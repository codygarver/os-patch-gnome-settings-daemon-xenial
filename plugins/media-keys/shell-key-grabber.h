/*
 * Generated by gdbus-codegen 2.44.1. DO NOT EDIT.
 *
 * The license of this code is the same as for the source it was derived from.
 */

#ifndef __SHELL_KEY_GRABBER_H__
#define __SHELL_KEY_GRABBER_H__

#include <gio/gio.h>

G_BEGIN_DECLS


/* ------------------------------------------------------------------------ */
/* Declarations for org.gnome.Shell */

#define SHELL_TYPE_KEY_GRABBER (shell_key_grabber_get_type ())
#define SHELL_KEY_GRABBER(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SHELL_TYPE_KEY_GRABBER, ShellKeyGrabber))
#define SHELL_IS_KEY_GRABBER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHELL_TYPE_KEY_GRABBER))
#define SHELL_KEY_GRABBER_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), SHELL_TYPE_KEY_GRABBER, ShellKeyGrabberIface))

struct _ShellKeyGrabber;
typedef struct _ShellKeyGrabber ShellKeyGrabber;
typedef struct _ShellKeyGrabberIface ShellKeyGrabberIface;

struct _ShellKeyGrabberIface
{
  GTypeInterface parent_iface;


  gboolean (*handle_grab_accelerator) (
    ShellKeyGrabber *object,
    GDBusMethodInvocation *invocation,
    const gchar *arg_accelerator,
    guint arg_flags);

  gboolean (*handle_grab_accelerators) (
    ShellKeyGrabber *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_accelerators);

  gboolean (*handle_ungrab_accelerator) (
    ShellKeyGrabber *object,
    GDBusMethodInvocation *invocation,
    guint arg_action);

  void (*accelerator_activated) (
    ShellKeyGrabber *object,
    guint arg_action,
    GVariant *arg_parameters);

};

GType shell_key_grabber_get_type (void) G_GNUC_CONST;

GDBusInterfaceInfo *shell_key_grabber_interface_info (void);
guint shell_key_grabber_override_properties (GObjectClass *klass, guint property_id_begin);


/* D-Bus method call completion functions: */
void shell_key_grabber_complete_grab_accelerator (
    ShellKeyGrabber *object,
    GDBusMethodInvocation *invocation,
    guint action);

void shell_key_grabber_complete_grab_accelerators (
    ShellKeyGrabber *object,
    GDBusMethodInvocation *invocation,
    GVariant *actions);

void shell_key_grabber_complete_ungrab_accelerator (
    ShellKeyGrabber *object,
    GDBusMethodInvocation *invocation,
    gboolean success);



/* D-Bus signal emissions functions: */
void shell_key_grabber_emit_accelerator_activated (
    ShellKeyGrabber *object,
    guint arg_action,
    GVariant *arg_parameters);



/* D-Bus method calls: */
void shell_key_grabber_call_grab_accelerator (
    ShellKeyGrabber *proxy,
    const gchar *arg_accelerator,
    guint arg_flags,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean shell_key_grabber_call_grab_accelerator_finish (
    ShellKeyGrabber *proxy,
    guint *out_action,
    GAsyncResult *res,
    GError **error);

gboolean shell_key_grabber_call_grab_accelerator_sync (
    ShellKeyGrabber *proxy,
    const gchar *arg_accelerator,
    guint arg_flags,
    guint *out_action,
    GCancellable *cancellable,
    GError **error);

void shell_key_grabber_call_grab_accelerators (
    ShellKeyGrabber *proxy,
    GVariant *arg_accelerators,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean shell_key_grabber_call_grab_accelerators_finish (
    ShellKeyGrabber *proxy,
    GVariant **out_actions,
    GAsyncResult *res,
    GError **error);

gboolean shell_key_grabber_call_grab_accelerators_sync (
    ShellKeyGrabber *proxy,
    GVariant *arg_accelerators,
    GVariant **out_actions,
    GCancellable *cancellable,
    GError **error);

void shell_key_grabber_call_ungrab_accelerator (
    ShellKeyGrabber *proxy,
    guint arg_action,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean shell_key_grabber_call_ungrab_accelerator_finish (
    ShellKeyGrabber *proxy,
    gboolean *out_success,
    GAsyncResult *res,
    GError **error);

gboolean shell_key_grabber_call_ungrab_accelerator_sync (
    ShellKeyGrabber *proxy,
    guint arg_action,
    gboolean *out_success,
    GCancellable *cancellable,
    GError **error);



/* ---- */

#define SHELL_TYPE_KEY_GRABBER_PROXY (shell_key_grabber_proxy_get_type ())
#define SHELL_KEY_GRABBER_PROXY(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SHELL_TYPE_KEY_GRABBER_PROXY, ShellKeyGrabberProxy))
#define SHELL_KEY_GRABBER_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHELL_TYPE_KEY_GRABBER_PROXY, ShellKeyGrabberProxyClass))
#define SHELL_KEY_GRABBER_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SHELL_TYPE_KEY_GRABBER_PROXY, ShellKeyGrabberProxyClass))
#define SHELL_IS_KEY_GRABBER_PROXY(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHELL_TYPE_KEY_GRABBER_PROXY))
#define SHELL_IS_KEY_GRABBER_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), SHELL_TYPE_KEY_GRABBER_PROXY))

typedef struct _ShellKeyGrabberProxy ShellKeyGrabberProxy;
typedef struct _ShellKeyGrabberProxyClass ShellKeyGrabberProxyClass;
typedef struct _ShellKeyGrabberProxyPrivate ShellKeyGrabberProxyPrivate;

struct _ShellKeyGrabberProxy
{
  /*< private >*/
  GDBusProxy parent_instance;
  ShellKeyGrabberProxyPrivate *priv;
};

struct _ShellKeyGrabberProxyClass
{
  GDBusProxyClass parent_class;
};

GType shell_key_grabber_proxy_get_type (void) G_GNUC_CONST;

void shell_key_grabber_proxy_new (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
ShellKeyGrabber *shell_key_grabber_proxy_new_finish (
    GAsyncResult        *res,
    GError             **error);
ShellKeyGrabber *shell_key_grabber_proxy_new_sync (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);

void shell_key_grabber_proxy_new_for_bus (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
ShellKeyGrabber *shell_key_grabber_proxy_new_for_bus_finish (
    GAsyncResult        *res,
    GError             **error);
ShellKeyGrabber *shell_key_grabber_proxy_new_for_bus_sync (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);


/* ---- */

#define SHELL_TYPE_KEY_GRABBER_SKELETON (shell_key_grabber_skeleton_get_type ())
#define SHELL_KEY_GRABBER_SKELETON(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SHELL_TYPE_KEY_GRABBER_SKELETON, ShellKeyGrabberSkeleton))
#define SHELL_KEY_GRABBER_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHELL_TYPE_KEY_GRABBER_SKELETON, ShellKeyGrabberSkeletonClass))
#define SHELL_KEY_GRABBER_SKELETON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SHELL_TYPE_KEY_GRABBER_SKELETON, ShellKeyGrabberSkeletonClass))
#define SHELL_IS_KEY_GRABBER_SKELETON(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHELL_TYPE_KEY_GRABBER_SKELETON))
#define SHELL_IS_KEY_GRABBER_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), SHELL_TYPE_KEY_GRABBER_SKELETON))

typedef struct _ShellKeyGrabberSkeleton ShellKeyGrabberSkeleton;
typedef struct _ShellKeyGrabberSkeletonClass ShellKeyGrabberSkeletonClass;
typedef struct _ShellKeyGrabberSkeletonPrivate ShellKeyGrabberSkeletonPrivate;

struct _ShellKeyGrabberSkeleton
{
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  ShellKeyGrabberSkeletonPrivate *priv;
};

struct _ShellKeyGrabberSkeletonClass
{
  GDBusInterfaceSkeletonClass parent_class;
};

GType shell_key_grabber_skeleton_get_type (void) G_GNUC_CONST;

ShellKeyGrabber *shell_key_grabber_skeleton_new (void);


G_END_DECLS

#endif /* __SHELL_KEY_GRABBER_H__ */
