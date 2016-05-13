/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <glib/gi18n.h>
#include <colord.h>
#include <gdk/gdk.h>
#include <stdlib.h>
#include <lcms2.h>
#include <canberra-gtk.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "gnome-settings-bus.h"
#include "gnome-settings-plugin.h"

#include "gsd-color-manager.h"
#include "gsd-color-state.h"
#include "gcm-edid.h"

#define GSD_COLOR_STATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_COLOR_STATE, GsdColorStatePrivate))

struct GsdColorStatePrivate
{
        GsdSessionManager *session;
        CdClient        *client;
        GnomeRRScreen   *state_screen;
        GHashTable      *edid_cache;
        GdkWindow       *gdk_window;
        gboolean         session_is_active;
        GHashTable      *device_assign_hash;
};

static void     gsd_color_state_class_init  (GsdColorStateClass *klass);
static void     gsd_color_state_init        (GsdColorState      *color_state);
static void     gsd_color_state_finalize    (GObject            *object);

G_DEFINE_TYPE (GsdColorState, gsd_color_state, G_TYPE_OBJECT)

/* see http://www.oyranos.org/wiki/index.php?title=ICC_Profiles_in_X_Specification_0.3 */
#define GCM_ICC_PROFILE_IN_X_VERSION_MAJOR      0
#define GCM_ICC_PROFILE_IN_X_VERSION_MINOR      3

typedef struct {
        guint32          red;
        guint32          green;
        guint32          blue;
} GnomeRROutputClutItem;

GQuark
gsd_color_state_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("gsd_color_state_error");
        return quark;
}

static GcmEdid *
gcm_session_get_output_edid (GsdColorState *state, GnomeRROutput *output, GError **error)
{
        const guint8 *data;
        gsize size;
        GcmEdid *edid = NULL;
        gboolean ret;

        /* can we find it in the cache */
        edid = g_hash_table_lookup (state->priv->edid_cache,
                                    gnome_rr_output_get_name (output));
        if (edid != NULL) {
                g_object_ref (edid);
                goto out;
        }

        /* parse edid */
        data = gnome_rr_output_get_edid_data (output, &size);
        if (data == NULL || size == 0) {
                g_set_error_literal (error,
                                     GNOME_RR_ERROR,
                                     GNOME_RR_ERROR_UNKNOWN,
                                     "unable to get EDID for output");
                goto out;
        }
        edid = gcm_edid_new ();
        ret = gcm_edid_parse (edid, data, size, error);
        if (!ret) {
                g_object_unref (edid);
                edid = NULL;
                goto out;
        }

        /* add to cache */
        g_hash_table_insert (state->priv->edid_cache,
                             g_strdup (gnome_rr_output_get_name (output)),
                             g_object_ref (edid));
out:
        return edid;
}

static gboolean
gcm_session_screen_set_icc_profile (GsdColorState *state,
                                    const gchar *filename,
                                    GError **error)
{
        gboolean ret = TRUE;
        gchar *data = NULL;
        gsize length;
        guint version_data;
        GsdColorStatePrivate *priv = state->priv;

        g_return_val_if_fail (filename != NULL, FALSE);

        /* wayland */
        if (priv->gdk_window == NULL) {
                g_debug ("not setting atom as running under wayland");
                goto out;
        }

        g_debug ("setting root window ICC profile atom from %s", filename);

        /* get contents of file */
        ret = g_file_get_contents (filename, &data, &length, error);
        if (!ret)
                goto out;

        /* set profile property */
        gdk_property_change (priv->gdk_window,
                             gdk_atom_intern_static_string ("_ICC_PROFILE"),
                             gdk_atom_intern_static_string ("CARDINAL"),
                             8,
                             GDK_PROP_MODE_REPLACE,
                             (const guchar *) data, length);

        /* set version property */
        version_data = GCM_ICC_PROFILE_IN_X_VERSION_MAJOR * 100 +
                        GCM_ICC_PROFILE_IN_X_VERSION_MINOR * 1;
        gdk_property_change (priv->gdk_window,
                             gdk_atom_intern_static_string ("_ICC_PROFILE_IN_X_VERSION"),
                             gdk_atom_intern_static_string ("CARDINAL"),
                             8,
                             GDK_PROP_MODE_REPLACE,
                             (const guchar *) &version_data, 1);
out:
        g_free (data);
        return ret;
}

static gchar *
gcm_session_get_output_id (GsdColorState *state, GnomeRROutput *output)
{
        const gchar *name;
        const gchar *serial;
        const gchar *vendor;
        GcmEdid *edid = NULL;
        GString *device_id;
        GError *error = NULL;

        /* all output devices are prefixed with this */
        device_id = g_string_new ("xrandr");

        /* get the output EDID if possible */
        edid = gcm_session_get_output_edid (state, output, &error);
        if (edid == NULL) {
                g_debug ("no edid for %s [%s], falling back to connection name",
                         gnome_rr_output_get_name (output),
                         error->message);
                g_error_free (error);
                g_string_append_printf (device_id,
                                        "-%s",
                                        gnome_rr_output_get_name (output));
                goto out;
        }

        /* check EDID data is okay to use */
        vendor = gcm_edid_get_vendor_name (edid);
        name = gcm_edid_get_monitor_name (edid);
        serial = gcm_edid_get_serial_number (edid);
        if (vendor == NULL && name == NULL && serial == NULL) {
                g_debug ("edid invalid for %s, falling back to connection name",
                         gnome_rr_output_get_name (output));
                g_string_append_printf (device_id,
                                        "-%s",
                                        gnome_rr_output_get_name (output));
                goto out;
        }

        /* use EDID data */
        if (vendor != NULL)
                g_string_append_printf (device_id, "-%s", vendor);
        if (name != NULL)
                g_string_append_printf (device_id, "-%s", name);
        if (serial != NULL)
                g_string_append_printf (device_id, "-%s", serial);
out:
        if (edid != NULL)
                g_object_unref (edid);
        return g_string_free (device_id, FALSE);
}

typedef struct {
        GsdColorState         *state;
        CdProfile               *profile;
        CdDevice                *device;
        guint32                  output_id;
} GcmSessionAsyncHelper;

static void
gcm_session_async_helper_free (GcmSessionAsyncHelper *helper)
{
        if (helper->state != NULL)
                g_object_unref (helper->state);
        if (helper->profile != NULL)
                g_object_unref (helper->profile);
        if (helper->device != NULL)
                g_object_unref (helper->device);
        g_free (helper);
}

static gboolean
gcm_utils_mkdir_for_filename (GFile *file, GError **error)
{
        gboolean ret = FALSE;
        GFile *parent_dir = NULL;

        /* get parent directory */
        parent_dir = g_file_get_parent (file);
        if (parent_dir == NULL) {
                g_set_error_literal (error,
                                     GSD_COLOR_MANAGER_ERROR,
                                     GSD_COLOR_MANAGER_ERROR_FAILED,
                                     "could not get parent dir");
                goto out;
        }

        /* ensure desination does not already exist */
        ret = g_file_query_exists (parent_dir, NULL);
        if (ret)
                goto out;
        ret = g_file_make_directory_with_parents (parent_dir, NULL, error);
        if (!ret)
                goto out;
out:
        if (parent_dir != NULL)
                g_object_unref (parent_dir);
        return ret;
}

static gboolean
gcm_apply_create_icc_profile_for_edid (GsdColorState *state,
                                       CdDevice *device,
                                       GcmEdid *edid,
                                       GFile *file,
                                       GError **error)
{
        CdIcc *icc = NULL;
        const gchar *data;
        gboolean ret = FALSE;
        GsdColorStatePrivate *priv = state->priv;

        /* ensure the per-user directory exists */
        ret = gcm_utils_mkdir_for_filename (file, error);
        if (!ret)
                goto out;

        /* create our generated profile */
        icc = cd_icc_new ();
        ret = cd_icc_create_from_edid (icc,
                                       gcm_edid_get_gamma (edid),
                                       gcm_edid_get_red (edid),
                                       gcm_edid_get_green (edid),
                                       gcm_edid_get_blue (edid),
                                       gcm_edid_get_white (edid),
                                       error);
        if (!ret)
                goto out;

        /* set copyright */
        cd_icc_set_copyright (icc, NULL,
                              /* deliberately not translated */
                              "This profile is free of known copyright restrictions.");

        /* set model and title */
        data = gcm_edid_get_monitor_name (edid);
        if (data == NULL)
                data = cd_client_get_system_model (priv->client);
        if (data == NULL)
                data = "Unknown monitor";
        cd_icc_set_model (icc, NULL, data);
        cd_icc_set_description (icc, NULL, data);

        /* get manufacturer */
        data = gcm_edid_get_vendor_name (edid);
        if (data == NULL)
                data = cd_client_get_system_vendor (priv->client);
        if (data == NULL)
                data = "Unknown vendor";
        cd_icc_set_manufacturer (icc, NULL, data);

        /* set the framework creator metadata */
        cd_icc_add_metadata (icc,
                             CD_PROFILE_METADATA_CMF_PRODUCT,
                             PACKAGE_NAME);
        cd_icc_add_metadata (icc,
                             CD_PROFILE_METADATA_CMF_BINARY,
                             PACKAGE_NAME);
        cd_icc_add_metadata (icc,
                             CD_PROFILE_METADATA_CMF_VERSION,
                             PACKAGE_VERSION);
        cd_icc_add_metadata (icc,
                             CD_PROFILE_METADATA_MAPPING_DEVICE_ID,
                             cd_device_get_id (device));

        /* set 'ICC meta Tag for Monitor Profiles' data */
        cd_icc_add_metadata (icc, CD_PROFILE_METADATA_EDID_MD5, gcm_edid_get_checksum (edid));
        data = gcm_edid_get_monitor_name (edid);
        if (data != NULL)
                cd_icc_add_metadata (icc, CD_PROFILE_METADATA_EDID_MODEL, data);
        data = gcm_edid_get_serial_number (edid);
        if (data != NULL)
                cd_icc_add_metadata (icc, CD_PROFILE_METADATA_EDID_SERIAL, data);
        data = gcm_edid_get_pnp_id (edid);
        if (data != NULL)
                cd_icc_add_metadata (icc, CD_PROFILE_METADATA_EDID_MNFT, data);
        data = gcm_edid_get_vendor_name (edid);
        if (data != NULL)
                cd_icc_add_metadata (icc, CD_PROFILE_METADATA_EDID_VENDOR, data);

        /* save */
        ret = cd_icc_save_file (icc, file, CD_ICC_SAVE_FLAGS_NONE, NULL, error);
        if (!ret)
                goto out;
out:
        if (icc != NULL)
                g_object_unref (icc);
        return ret;
}

static GPtrArray *
gcm_session_generate_vcgt (CdProfile *profile, guint size)
{
        GnomeRROutputClutItem *tmp;
        GPtrArray *array = NULL;
        const cmsToneCurve **vcgt;
        cmsFloat32Number in;
        guint i;
        cmsHPROFILE lcms_profile;
        CdIcc *icc = NULL;

        /* invalid size */
        if (size == 0)
                goto out;

        /* open file */
        icc = cd_profile_load_icc (profile, CD_ICC_LOAD_FLAGS_NONE, NULL, NULL);
        if (icc == NULL)
                goto out;

        /* get tone curves from profile */
        lcms_profile = cd_icc_get_handle (icc);
        vcgt = cmsReadTag (lcms_profile, cmsSigVcgtTag);
        if (vcgt == NULL || vcgt[0] == NULL) {
                g_debug ("profile does not have any VCGT data");
                goto out;
        }

        /* create array */
        array = g_ptr_array_new_with_free_func (g_free);
        for (i = 0; i < size; i++) {
                in = (gdouble) i / (gdouble) (size - 1);
                tmp = g_new0 (GnomeRROutputClutItem, 1);
                tmp->red = cmsEvalToneCurveFloat(vcgt[0], in) * (gdouble) 0xffff;
                tmp->green = cmsEvalToneCurveFloat(vcgt[1], in) * (gdouble) 0xffff;
                tmp->blue = cmsEvalToneCurveFloat(vcgt[2], in) * (gdouble) 0xffff;
                g_ptr_array_add (array, tmp);
        }
out:
        if (icc != NULL)
                g_object_unref (icc);
        return array;
}

static guint
gnome_rr_output_get_gamma_size (GnomeRROutput *output)
{
        GnomeRRCrtc *crtc;
        gint len = 0;

        crtc = gnome_rr_output_get_crtc (output);
        if (crtc == NULL)
                return 0;
        gnome_rr_crtc_get_gamma (crtc,
                                 &len,
                                 NULL, NULL, NULL);
        return (guint) len;
}

static gboolean
gcm_session_output_set_gamma (GnomeRROutput *output,
                              GPtrArray *array,
                              GError **error)
{
        gboolean ret = TRUE;
        guint16 *red = NULL;
        guint16 *green = NULL;
        guint16 *blue = NULL;
        guint i;
        GnomeRROutputClutItem *data;
        GnomeRRCrtc *crtc;

        /* no length? */
        if (array->len == 0) {
                ret = FALSE;
                g_set_error_literal (error,
                                     GSD_COLOR_MANAGER_ERROR,
                                     GSD_COLOR_MANAGER_ERROR_FAILED,
                                     "no data in the CLUT array");
                goto out;
        }

        /* convert to a type X understands */
        red = g_new (guint16, array->len);
        green = g_new (guint16, array->len);
        blue = g_new (guint16, array->len);
        for (i = 0; i < array->len; i++) {
                data = g_ptr_array_index (array, i);
                red[i] = data->red;
                green[i] = data->green;
                blue[i] = data->blue;
        }

        /* send to LUT */
        crtc = gnome_rr_output_get_crtc (output);
        if (crtc == NULL) {
                ret = FALSE;
                g_set_error (error,
                             GSD_COLOR_MANAGER_ERROR,
                             GSD_COLOR_MANAGER_ERROR_FAILED,
                             "failed to get ctrc for %s",
                             gnome_rr_output_get_name (output));
                goto out;
        }
        gnome_rr_crtc_set_gamma (crtc, array->len,
                                 red, green, blue);
out:
        g_free (red);
        g_free (green);
        g_free (blue);
        return ret;
}

static gboolean
gcm_session_device_set_gamma (GnomeRROutput *output,
                              CdProfile *profile,
                              GError **error)
{
        gboolean ret = FALSE;
        guint size;
        GPtrArray *clut = NULL;

        /* create a lookup table */
        size = gnome_rr_output_get_gamma_size (output);
        if (size == 0) {
                ret = TRUE;
                goto out;
        }
        clut = gcm_session_generate_vcgt (profile, size);
        if (clut == NULL) {
                g_set_error_literal (error,
                                     GSD_COLOR_MANAGER_ERROR,
                                     GSD_COLOR_MANAGER_ERROR_FAILED,
                                     "failed to generate vcgt");
                goto out;
        }

        /* apply the vcgt to this output */
        ret = gcm_session_output_set_gamma (output, clut, error);
        if (!ret)
                goto out;
out:
        if (clut != NULL)
                g_ptr_array_unref (clut);
        return ret;
}

static gboolean
gcm_session_device_reset_gamma (GnomeRROutput *output,
                                GError **error)
{
        gboolean ret;
        guint i;
        guint size;
        guint32 value;
        GPtrArray *clut;
        GnomeRROutputClutItem *data;

        /* create a linear ramp */
        g_debug ("falling back to dummy ramp");
        clut = g_ptr_array_new_with_free_func (g_free);
        size = gnome_rr_output_get_gamma_size (output);
        if (size == 0) {
                ret = TRUE;
                goto out;
        }
        for (i = 0; i < size; i++) {
                value = (i * 0xffff) / (size - 1);
                data = g_new0 (GnomeRROutputClutItem, 1);
                data->red = value;
                data->green = value;
                data->blue = value;
                g_ptr_array_add (clut, data);
        }

        /* apply the vcgt to this output */
        ret = gcm_session_output_set_gamma (output, clut, error);
        if (!ret)
                goto out;
out:
        g_ptr_array_unref (clut);
        return ret;
}

static GnomeRROutput *
gcm_session_get_state_output_by_id (GsdColorState *state,
                                  const gchar *device_id,
                                  GError **error)
{
        gchar *output_id;
        GnomeRROutput *output = NULL;
        GnomeRROutput **outputs = NULL;
        guint i;
        GsdColorStatePrivate *priv = state->priv;

        /* search all STATE outputs for the device id */
        outputs = gnome_rr_screen_list_outputs (priv->state_screen);
        if (outputs == NULL) {
                g_set_error_literal (error,
                                     GSD_COLOR_MANAGER_ERROR,
                                     GSD_COLOR_MANAGER_ERROR_FAILED,
                                     "Failed to get outputs");
                goto out;
        }
        for (i = 0; outputs[i] != NULL && output == NULL; i++) {
                output_id = gcm_session_get_output_id (state, outputs[i]);
                if (g_strcmp0 (output_id, device_id) == 0)
                        output = outputs[i];
                g_free (output_id);
        }
        if (output == NULL) {
                g_set_error (error,
                             GSD_COLOR_MANAGER_ERROR,
                             GSD_COLOR_MANAGER_ERROR_FAILED,
                             "Failed to find output %s",
                             device_id);
        }
out:
        return output;
}

/* this function is more complicated than it should be, due to the
 * fact that XOrg sometimes assigns no primary devices when using
 * "xrandr --auto" or when the version of RANDR is < 1.3 */
static gboolean
gcm_session_use_output_profile_for_screen (GsdColorState *state,
                                           GnomeRROutput *output)
{
        gboolean has_laptop = FALSE;
        gboolean has_primary = FALSE;
        GnomeRROutput **outputs;
        GnomeRROutput *connected = NULL;
        guint i;

        /* do we have any screens marked as primary */
        outputs = gnome_rr_screen_list_outputs (state->priv->state_screen);
        if (outputs == NULL || outputs[0] == NULL) {
                g_warning ("failed to get outputs");
                return FALSE;
        }
        for (i = 0; outputs[i] != NULL; i++) {
                if (connected == NULL)
                        connected = outputs[i];
                if (gnome_rr_output_get_is_primary (outputs[i]))
                        has_primary = TRUE;
                if (gnome_rr_output_is_builtin_display (outputs[i]))
                        has_laptop = TRUE;
        }

        /* we have an assigned primary device, are we that? */
        if (has_primary)
                return gnome_rr_output_get_is_primary (output);

        /* choosing the internal panel is probably sane */
        if (has_laptop)
                return gnome_rr_output_is_builtin_display (output);

        /* we have to choose one, so go for the first connected device */
        if (connected != NULL)
                return gnome_rr_output_get_id (connected) == gnome_rr_output_get_id (output);

        return FALSE;
}

/* TODO: remove when we can dep on a released version of colord */
#ifndef CD_PROFILE_METADATA_SCREEN_BRIGHTNESS
#define CD_PROFILE_METADATA_SCREEN_BRIGHTNESS		"SCREEN_brightness"
#endif

#define GSD_DBUS_NAME_POWER		GSD_DBUS_NAME ".Power"
#define GSD_DBUS_INTERFACE_POWER_SCREEN	GSD_DBUS_BASE_INTERFACE ".Power.Screen"
#define GSD_DBUS_PATH_POWER		GSD_DBUS_PATH "/Power"

static void
gcm_session_set_output_percentage (guint percentage)
{
        GDBusConnection *connection;

        /* get a ref to the existing bus connection */
        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
        if (connection == NULL)
                return;
        g_dbus_connection_call (connection,
                                GSD_DBUS_NAME_POWER,
                                GSD_DBUS_PATH_POWER,
                                "org.freedesktop.DBus.Properties",
                                "Set",
                                g_variant_new_parsed ("('" GSD_DBUS_INTERFACE_POWER_SCREEN "',"
                                                      "'Brightness', %v)",
                                                      g_variant_new_int32 (percentage)),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1, NULL, NULL, NULL);
        g_object_unref (connection);
}

static void
gcm_session_device_assign_profile_connect_cb (GObject *object,
                                              GAsyncResult *res,
                                              gpointer user_data)
{
        CdProfile *profile = CD_PROFILE (object);
        const gchar *brightness_profile;
        const gchar *filename;
        gboolean ret;
        GError *error = NULL;
        GnomeRROutput *output;
        guint brightness_percentage;
        GcmSessionAsyncHelper *helper = (GcmSessionAsyncHelper *) user_data;
        GsdColorState *state = GSD_COLOR_STATE (helper->state);

        /* get properties */
        ret = cd_profile_connect_finish (profile, res, &error);
        if (!ret) {
                g_warning ("failed to connect to profile: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* get the filename */
        filename = cd_profile_get_filename (profile);
        g_assert (filename != NULL);

        /* get the output (can't save in helper as GnomeRROutput isn't
         * a GObject, just a pointer */
        output = gnome_rr_screen_get_output_by_id (state->priv->state_screen,
                                                   helper->output_id);
        if (output == NULL)
                goto out;

        /* if output is a laptop screen and the profile has a
         * calibration brightness then set this new brightness */
        brightness_profile = cd_profile_get_metadata_item (profile,
                                                           CD_PROFILE_METADATA_SCREEN_BRIGHTNESS);
        if (gnome_rr_output_is_builtin_display (output) &&
            brightness_profile != NULL) {
                /* the percentage is stored in the profile metadata as
                 * a string, not ideal, but it's all we have... */
                brightness_percentage = atoi (brightness_profile);
                gcm_session_set_output_percentage (brightness_percentage);
        }

        /* set the _ICC_PROFILE atom */
        ret = gcm_session_use_output_profile_for_screen (state, output);
        if (ret) {
                ret = gcm_session_screen_set_icc_profile (state,
                                                          filename,
                                                          &error);
                if (!ret) {
                        g_warning ("failed to set screen _ICC_PROFILE: %s",
                                   error->message);
                        g_clear_error (&error);
                }
        }

        /* create a vcgt for this icc file */
        ret = cd_profile_get_has_vcgt (profile);
        if (ret) {
                ret = gcm_session_device_set_gamma (output,
                                                    profile,
                                                    &error);
                if (!ret) {
                        g_warning ("failed to set %s gamma tables: %s",
                                   cd_device_get_id (helper->device),
                                   error->message);
                        g_error_free (error);
                        goto out;
                }
        } else {
                ret = gcm_session_device_reset_gamma (output,
                                                      &error);
                if (!ret) {
                        g_warning ("failed to reset %s gamma tables: %s",
                                   cd_device_get_id (helper->device),
                                   error->message);
                        g_error_free (error);
                        goto out;
                }
        }
out:
        gcm_session_async_helper_free (helper);
}

/*
 * Check to see if the on-disk profile has the MAPPING_device_id
 * metadata, and if not, we should delete the profile and re-create it
 * so that it gets mapped by the daemon.
 */
static gboolean
gcm_session_check_profile_device_md (GFile *file)
{
        const gchar *key_we_need = CD_PROFILE_METADATA_MAPPING_DEVICE_ID;
        CdIcc *icc;
        gboolean ret;

        icc = cd_icc_new ();
        ret = cd_icc_load_file (icc, file, CD_ICC_LOAD_FLAGS_METADATA, NULL, NULL);
        if (!ret)
                goto out;
        ret = cd_icc_get_metadata_item (icc, key_we_need) != NULL;
        if (!ret) {
                g_debug ("auto-edid profile is old, and contains no %s data",
                         key_we_need);
        }
out:
        g_object_unref (icc);
        return ret;
}

static void
gcm_session_device_assign_connect_cb (GObject *object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
        CdDeviceKind kind;
        CdProfile *profile = NULL;
        gboolean ret;
        gchar *autogen_filename = NULL;
        gchar *autogen_path = NULL;
        GcmEdid *edid = NULL;
        GnomeRROutput *output = NULL;
        GError *error = NULL;
        GFile *file = NULL;
        const gchar *xrandr_id;
        GcmSessionAsyncHelper *helper;
        CdDevice *device = CD_DEVICE (object);
        GsdColorState *state = GSD_COLOR_STATE (user_data);
        GsdColorStatePrivate *priv = state->priv;

        /* remove from assign array */
        g_hash_table_remove (state->priv->device_assign_hash,
                             cd_device_get_object_path (device));

        /* get properties */
        ret = cd_device_connect_finish (device, res, &error);
        if (!ret) {
                g_warning ("failed to connect to device: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* check we care */
        kind = cd_device_get_kind (device);
        if (kind != CD_DEVICE_KIND_DISPLAY)
                goto out;

        g_debug ("need to assign display device %s",
                 cd_device_get_id (device));

        /* get the GnomeRROutput for the device id */
        xrandr_id = cd_device_get_id (device);
        output = gcm_session_get_state_output_by_id (state,
                                                   xrandr_id,
                                                   &error);
        if (output == NULL) {
                g_warning ("no %s device found: %s",
                           cd_device_get_id (device),
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* create profile from device edid if it exists */
        edid = gcm_session_get_output_edid (state, output, &error);
        if (edid == NULL) {
                g_warning ("unable to get EDID for %s: %s",
                           cd_device_get_id (device),
                           error->message);
                g_clear_error (&error);

        } else {
                autogen_filename = g_strdup_printf ("edid-%s.icc",
                                                    gcm_edid_get_checksum (edid));
                autogen_path = g_build_filename (g_get_user_data_dir (),
                                                 "icc", autogen_filename, NULL);

                /* check if auto-profile has up-to-date metadata */
                file = g_file_new_for_path (autogen_path);
                if (gcm_session_check_profile_device_md (file)) {
                        g_debug ("auto-profile edid %s exists with md", autogen_path);
                } else {
                        g_debug ("auto-profile edid does not exist, creating as %s",
                                 autogen_path);
                        ret = gcm_apply_create_icc_profile_for_edid (state,
                                                                     device,
                                                                     edid,
                                                                     file,
                                                                     &error);
                        if (!ret) {
                                g_warning ("failed to create profile from EDID data: %s",
                                             error->message);
                                g_clear_error (&error);
                        }
                }
        }

        /* get the default profile for the device */
        profile = cd_device_get_default_profile (device);
        if (profile == NULL) {
                g_debug ("%s has no default profile to set",
                         cd_device_get_id (device));

                /* the default output? */
                if (gnome_rr_output_get_is_primary (output) &&
                    priv->gdk_window != NULL) {
                        gdk_property_delete (priv->gdk_window,
                                             gdk_atom_intern_static_string ("_ICC_PROFILE"));
                        gdk_property_delete (priv->gdk_window,
                                             gdk_atom_intern_static_string ("_ICC_PROFILE_IN_X_VERSION"));
                }

                /* reset, as we want linear profiles for profiling */
                ret = gcm_session_device_reset_gamma (output,
                                                      &error);
                if (!ret) {
                        g_warning ("failed to reset %s gamma tables: %s",
                                   cd_device_get_id (device),
                                   error->message);
                        g_error_free (error);
                        goto out;
                }
                goto out;
        }

        /* get properties */
        helper = g_new0 (GcmSessionAsyncHelper, 1);
        helper->output_id = gnome_rr_output_get_id (output);
        helper->state = g_object_ref (state);
        helper->device = g_object_ref (device);
        cd_profile_connect (profile,
                            NULL,
                            gcm_session_device_assign_profile_connect_cb,
                            helper);
out:
        g_free (autogen_filename);
        g_free (autogen_path);
        if (file != NULL)
                g_object_unref (file);
        if (edid != NULL)
                g_object_unref (edid);
        if (profile != NULL)
                g_object_unref (profile);
}

static void
gcm_session_device_assign (GsdColorState *state, CdDevice *device)
{
        const gchar *key;
        gpointer found;

        /* are we already assigning this device */
        key = cd_device_get_object_path (device);
        found = g_hash_table_lookup (state->priv->device_assign_hash, key);
        if (found != NULL) {
                g_debug ("assign for %s already in progress", key);
                return;
        }
        g_hash_table_insert (state->priv->device_assign_hash,
                             g_strdup (key),
                             GINT_TO_POINTER (TRUE));
        cd_device_connect (device,
                           NULL,
                           gcm_session_device_assign_connect_cb,
                           state);
}

static void
gcm_session_device_added_assign_cb (CdClient *client,
                                    CdDevice *device,
                                    GsdColorState *state)
{
        gcm_session_device_assign (state, device);
}

static void
gcm_session_device_changed_assign_cb (CdClient *client,
                                      CdDevice *device,
                                      GsdColorState *state)
{
        g_debug ("%s changed", cd_device_get_object_path (device));
        gcm_session_device_assign (state, device);
}

static void
gcm_session_create_device_cb (GObject *object,
                              GAsyncResult *res,
                              gpointer user_data)
{
        CdDevice *device;
        GError *error = NULL;

        device = cd_client_create_device_finish (CD_CLIENT (object),
                                                 res,
                                                 &error);
        if (device == NULL) {
                if (error->domain != CD_CLIENT_ERROR ||
                    error->code != CD_CLIENT_ERROR_ALREADY_EXISTS) {
                        g_warning ("failed to create device: %s",
                                   error->message);
                }
                g_error_free (error);
                return;
        }
        g_object_unref (device);
}

static void
gcm_session_add_state_output (GsdColorState *state, GnomeRROutput *output)
{
        const gchar *edid_checksum = NULL;
        const gchar *model = NULL;
        const gchar *serial = NULL;
        const gchar *vendor = NULL;
        gboolean ret;
        gchar *device_id = NULL;
        GcmEdid *edid;
        GError *error = NULL;
        GHashTable *device_props = NULL;
        GsdColorStatePrivate *priv = state->priv;

        /* try to get edid */
        edid = gcm_session_get_output_edid (state, output, &error);
        if (edid == NULL) {
                g_warning ("failed to get edid: %s",
                           error->message);
                g_clear_error (&error);
        }

        /* prefer DMI data for the internal output */
        ret = gnome_rr_output_is_builtin_display (output);
        if (ret) {
                model = cd_client_get_system_model (priv->client);
                vendor = cd_client_get_system_vendor (priv->client);
        }

        /* use EDID data if we have it */
        if (edid != NULL) {
                edid_checksum = gcm_edid_get_checksum (edid);
                if (model == NULL)
                        model = gcm_edid_get_monitor_name (edid);
                if (vendor == NULL)
                        vendor = gcm_edid_get_vendor_name (edid);
                if (serial == NULL)
                        serial = gcm_edid_get_serial_number (edid);
        }

        /* ensure mandatory fields are set */
        if (model == NULL)
                model = gnome_rr_output_get_name (output);
        if (vendor == NULL)
                vendor = "unknown";
        if (serial == NULL)
                serial = "unknown";

        device_id = gcm_session_get_output_id (state, output);
        g_debug ("output %s added", device_id);
        device_props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              NULL, NULL);
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_PROPERTY_KIND,
                             (gpointer) cd_device_kind_to_string (CD_DEVICE_KIND_DISPLAY));
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_PROPERTY_MODE,
                             (gpointer) cd_device_mode_to_string (CD_DEVICE_MODE_PHYSICAL));
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_PROPERTY_COLORSPACE,
                             (gpointer) cd_colorspace_to_string (CD_COLORSPACE_RGB));
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_PROPERTY_VENDOR,
                             (gpointer) vendor);
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_PROPERTY_MODEL,
                             (gpointer) model);
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_PROPERTY_SERIAL,
                             (gpointer) serial);
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_METADATA_XRANDR_NAME,
                             (gpointer) gnome_rr_output_get_name (output));
        g_hash_table_insert (device_props,
                             (gpointer) CD_DEVICE_METADATA_OUTPUT_PRIORITY,
                             gnome_rr_output_get_is_primary (output) ?
                             (gpointer) CD_DEVICE_METADATA_OUTPUT_PRIORITY_PRIMARY :
                             (gpointer) CD_DEVICE_METADATA_OUTPUT_PRIORITY_SECONDARY);
        if (edid_checksum != NULL) {
                g_hash_table_insert (device_props,
                                     (gpointer) CD_DEVICE_METADATA_OUTPUT_EDID_MD5,
                                     (gpointer) edid_checksum);
        }
        /* set this so we can call the device a 'Laptop Screen' in the
         * control center main panel */
        if (gnome_rr_output_is_builtin_display (output)) {
                g_hash_table_insert (device_props,
                                     (gpointer) CD_DEVICE_PROPERTY_EMBEDDED,
                                     NULL);
        }
        cd_client_create_device (priv->client,
                                 device_id,
                                 CD_OBJECT_SCOPE_TEMP,
                                 device_props,
                                 NULL,
                                 gcm_session_create_device_cb,
                                 state);
        g_free (device_id);
        if (device_props != NULL)
                g_hash_table_unref (device_props);
        if (edid != NULL)
                g_object_unref (edid);
}


static void
gnome_rr_screen_output_added_cb (GnomeRRScreen *screen,
                                GnomeRROutput *output,
                                GsdColorState *state)
{
        gcm_session_add_state_output (state, output);
}

static void
gcm_session_screen_removed_delete_device_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;

        /* deleted device */
        ret = cd_client_delete_device_finish (CD_CLIENT (object),
                                              res,
                                              &error);
        if (!ret) {
                g_warning ("failed to delete device: %s",
                           error->message);
                g_error_free (error);
        }
}

static void
gcm_session_screen_removed_find_device_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
        GError *error = NULL;
        CdDevice *device;
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        device = cd_client_find_device_finish (state->priv->client,
                                               res,
                                               &error);
        if (device == NULL) {
                g_warning ("failed to find device: %s",
                           error->message);
                g_error_free (error);
                return;
        }
        g_debug ("output %s found, and will be removed",
                 cd_device_get_object_path (device));
        cd_client_delete_device (state->priv->client,
                                 device,
                                 NULL,
                                 gcm_session_screen_removed_delete_device_cb,
                                 state);
        g_object_unref (device);
}

static void
gnome_rr_screen_output_removed_cb (GnomeRRScreen *screen,
                                   GnomeRROutput *output,
                                   GsdColorState *state)
{
        g_debug ("output %s removed",
                 gnome_rr_output_get_name (output));
        g_hash_table_remove (state->priv->edid_cache,
                             gnome_rr_output_get_name (output));
        cd_client_find_device_by_property (state->priv->client,
                                           CD_DEVICE_METADATA_XRANDR_NAME,
                                           gnome_rr_output_get_name (output),
                                           NULL,
                                           gcm_session_screen_removed_find_device_cb,
                                           state);
}

static void
gcm_session_get_devices_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
        CdDevice *device;
        GError *error = NULL;
        GPtrArray *array;
        guint i;
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        array = cd_client_get_devices_finish (CD_CLIENT (object), res, &error);
        if (array == NULL) {
                g_warning ("failed to get devices: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }
        for (i = 0; i < array->len; i++) {
                device = g_ptr_array_index (array, i);
                gcm_session_device_assign (state, device);
        }
out:
        if (array != NULL)
                g_ptr_array_unref (array);
}

static void
gcm_session_profile_gamma_find_device_cb (GObject *object,
                                          GAsyncResult *res,
                                          gpointer user_data)
{
        CdClient *client = CD_CLIENT (object);
        CdDevice *device = NULL;
        GError *error = NULL;
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        device = cd_client_find_device_by_property_finish (client,
                                                           res,
                                                           &error);
        if (device == NULL) {
                g_warning ("could not find device: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* get properties */
        cd_device_connect (device,
                           NULL,
                           gcm_session_device_assign_connect_cb,
                           state);
out:
        if (device != NULL)
                g_object_unref (device);
}

/* We have to reset the gamma tables each time as if the primary output
 * has changed then different crtcs are going to be used.
 * See https://bugzilla.gnome.org/show_bug.cgi?id=660164 for an example */
static void
gnome_rr_screen_output_changed_cb (GnomeRRScreen *screen,
                                   GsdColorState *state)
{
        GnomeRROutput **outputs;
        GsdColorStatePrivate *priv = state->priv;
        guint i;

        /* get STATE outputs */
        outputs = gnome_rr_screen_list_outputs (priv->state_screen);
        if (outputs == NULL) {
                g_warning ("failed to get outputs");
                return;
        }
        for (i = 0; outputs[i] != NULL; i++) {
                /* get CdDevice for this output */
                cd_client_find_device_by_property (state->priv->client,
                                                   CD_DEVICE_METADATA_XRANDR_NAME,
                                                   gnome_rr_output_get_name (outputs[i]),
                                                   NULL,
                                                   gcm_session_profile_gamma_find_device_cb,
                                                   state);
        }

}

static gboolean
has_changed (char       **strv,
	     const char  *str)
{
        guint i;
        for (i = 0; strv[i] != NULL; i++) {
                if (g_str_equal (str, strv[i]))
                        return TRUE;
        }
        return FALSE;
}

static void
gcm_session_active_changed_cb (GDBusProxy      *session,
                               GVariant        *changed,
                               char           **invalidated,
                               GsdColorState *state)
{
        GsdColorStatePrivate *priv = state->priv;
        GVariant *active_v = NULL;
        gboolean is_active;

        if (has_changed (invalidated, "SessionIsActive"))
                return;

        /* not yet connected to the daemon */
        if (!cd_client_get_connected (priv->client))
                return;

        active_v = g_dbus_proxy_get_cached_property (session, "SessionIsActive");
        g_return_if_fail (active_v != NULL);
        is_active = g_variant_get_boolean (active_v);
        g_variant_unref (active_v);

        /* When doing the fast-user-switch into a new account, load the
         * new users chosen profiles.
         *
         * If this is the first time the GnomeSettingsSession has been
         * loaded, then we'll get a change from unknown to active
         * and we want to avoid reprobing the devices for that.
         */
        if (is_active && !priv->session_is_active) {
                g_debug ("Done switch to new account, reload devices");
                cd_client_get_devices (state->priv->client, NULL,
                                       gcm_session_get_devices_cb,
                                       state);
        }
        priv->session_is_active = is_active;
}

static void
gcm_session_client_connect_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        GnomeRROutput **outputs;
        guint i;
        GsdColorState *state = GSD_COLOR_STATE (user_data);
        GsdColorStatePrivate *priv = state->priv;

        /* connected */
        ret = cd_client_connect_finish (state->priv->client, res, &error);
        if (!ret) {
                g_warning ("failed to connect to colord: %s", error->message);
                g_error_free (error);
                return;
        }

        /* is there an available colord instance? */
        ret = cd_client_get_has_server (state->priv->client);
        if (!ret) {
                g_warning ("There is no colord server available");
                goto out;
        }

        /* watch if sessions change */
        g_signal_connect (priv->session, "g-properties-changed",
                          G_CALLBACK (gcm_session_active_changed_cb), state);

        /* add screens */
        gnome_rr_screen_refresh (priv->state_screen, &error);
        if (error != NULL) {
                g_warning ("failed to refresh: %s", error->message);
                g_error_free (error);
                goto out;
        }

        /* get STATE outputs */
        outputs = gnome_rr_screen_list_outputs (priv->state_screen);
        if (outputs == NULL) {
                g_warning ("failed to get outputs");
                goto out;
        }
        for (i = 0; outputs[i] != NULL; i++) {
                gcm_session_add_state_output (state, outputs[i]);
        }

        /* only connect when colord is awake */
        g_signal_connect (priv->state_screen, "output-connected",
                          G_CALLBACK (gnome_rr_screen_output_added_cb),
                          state);
        g_signal_connect (priv->state_screen, "output-disconnected",
                          G_CALLBACK (gnome_rr_screen_output_removed_cb),
                          state);
        g_signal_connect (priv->state_screen, "changed",
                          G_CALLBACK (gnome_rr_screen_output_changed_cb),
                          state);

        g_signal_connect (priv->client, "device-added",
                          G_CALLBACK (gcm_session_device_added_assign_cb),
                          state);
        g_signal_connect (priv->client, "device-changed",
                          G_CALLBACK (gcm_session_device_changed_assign_cb),
                          state);

        /* set for each device that already exist */
        cd_client_get_devices (priv->client, NULL,
                               gcm_session_get_devices_cb,
                               state);
out:
        return;
}

static void
on_rr_screen_acquired (GObject      *object,
                       GAsyncResult *result,
                       gpointer      data)
{
        GsdColorState *state = data;
        GsdColorStatePrivate *priv = state->priv;
        GnomeRRScreen *screen;

        screen = gnome_rr_screen_new_finish (result, NULL);
        if (!screen)
                return;

        priv->state_screen = screen;

        cd_client_connect (priv->client,
                           NULL,
                           gcm_session_client_connect_cb,
                           state);
}

void
gsd_color_state_start (GsdColorState *state)
{
        /* coldplug the list of screens */
        gnome_rr_screen_new_async (gdk_screen_get_default (),
                                   on_rr_screen_acquired,
                                   state);
}

static void
gsd_color_state_class_init (GsdColorStateClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_color_state_finalize;

        g_type_class_add_private (klass, sizeof (GsdColorStatePrivate));
}

static void
gsd_color_state_init (GsdColorState *state)
{
        GsdColorStatePrivate *priv;
        priv = state->priv = GSD_COLOR_STATE_GET_PRIVATE (state);

        /* track the active session */
        priv->session = gnome_settings_bus_get_session_proxy ();

#ifdef GDK_WINDOWING_X11
        /* set the _ICC_PROFILE atoms on the root screen */
        if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
                priv->gdk_window = gdk_screen_get_root_window (gdk_screen_get_default ());
#endif

        /* parsing the EDID is expensive */
        priv->edid_cache = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  g_object_unref);

        /* we don't want to assign devices multiple times at startup */
        priv->device_assign_hash = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          NULL);

        priv->client = cd_client_new ();
}

static void
gsd_color_state_finalize (GObject *object)
{
        GsdColorState *state;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_STATE (object));

        state = GSD_COLOR_STATE (object);

        g_clear_object (&state->priv->client);
        g_clear_object (&state->priv->session);
        g_clear_pointer (&state->priv->edid_cache, g_hash_table_destroy);
        g_clear_pointer (&state->priv->device_assign_hash, g_hash_table_destroy);
        g_clear_object (&state->priv->state_screen);

        G_OBJECT_CLASS (gsd_color_state_parent_class)->finalize (object);
}

GsdColorState *
gsd_color_state_new (void)
{
        GsdColorState *state;
        state = g_object_new (GSD_TYPE_COLOR_STATE, NULL);
        return GSD_COLOR_STATE (state);
}
