#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>

#include <stdlib.h>

#include "icd_script.h"
#include "icd_log.h"

#define ICD_SCRIPT_MIN_TIMEOUT   1
#define ICD_SCRIPT_DEFAULT_TIMEOUT   15
#define ICD_SCRIPT_MAX_TIMEOUT   120
#define ICD_SCRIPT_GCONF_PATH   ICD_GCONF_SETTINGS "/network_scripts/timeout"
#define SCRIPT_IFACE   "IFACE"
#define SCRIPT_LOGICAL   "LOGICAL"
#define SCRIPT_ADDRFAM   "ADDRFAM"
#define SCRIPT_METHOD   "METHOD"
#define SCRIPT_MODE   "MODE"
#define SCRIPT_PHASE   "PHASE"
#define SCRIPT_VERBOSITY   "VERBOSITY"
#define SCRIPT_VERBOSITY_VALUE   "0"
#define SCRIPT_PATH   "PATH"
#define SCRIPT_PATH_VALUE
#define SCRIPT_IAP_ID   "ICD_CONNECTION_ID"
#define SCRIPT_IAP_TYPE   "ICD_CONNECTION_TYPE"
#define SCRIPT_PROXY_UNSET   "ICD_PROXY_UNSET"

static GSList *script_data = NULL;

#define SETENV(e, v) \
  setenv(e, v, 1); \
  ILOG_DEBUG(e"=%s", v);

struct icd_script_data {
  pid_t pid;
  guint timeout_id;
  icd_script_cb_fn cb;
  gpointer user_data;
};

static GSList **
icd_script_get()
{
  return &script_data;
}

/**
 * @brief Read the script timeout value from gconf
 *
 * @return script timeout value between #ICD_SCRIPT_MIN_TIMEOUT and
 * #ICD_SCRIPT_MAX_TIMEOUT.
 *
 */
static gint
icd_script_timeout_secs (void)
{
  gint timeout;
  GError *err = NULL;
  GConfClient *gconf = gconf_client_get_default();
  GConfValue *val = gconf_client_get(gconf, ICD_SCRIPT_GCONF_PATH, &err);

  g_object_unref(gconf);

  if (err || !G_VALUE_HOLDS_INT(val))
  {
    ILOG_INFO("script timeout value not set, using default %ds",
              ICD_SCRIPT_DEFAULT_TIMEOUT);

    if (err)
      g_error_free(err);

    timeout = ICD_SCRIPT_DEFAULT_TIMEOUT ;
  }
  else
  {
    timeout = gconf_value_get_int(val);
    gconf_value_free(val);

    if (timeout < ICD_SCRIPT_MIN_TIMEOUT || timeout > ICD_SCRIPT_MAX_TIMEOUT)
    {
      ILOG_WARN("script timeout %d not in range %d-%d, reset to %ds",
                timeout, ICD_SCRIPT_MIN_TIMEOUT, ICD_SCRIPT_MAX_TIMEOUT,
                ICD_SCRIPT_DEFAULT_TIMEOUT);
    }
  }

  return timeout * 1000;
}

/**
 * @brief Script timeout function
 *
 * @param data pointer to pid of the script that timed out
 *
 * @return FALSE in order not to run again
 *
 */
static gboolean
icd_script_timeout(gpointer data)
{
  pid_t pid = *(pid_t *)data;

  g_free(data);
  ILOG_INFO("script exceeded time to live value, killed");

  kill(pid, SIGKILL);
  return FALSE;
}

/**
 * @brief Forks and execs the network script
 *
 * @param script the script, i.e. 'pre-up', 'post-up', 'pre-down', 'post-down'
 * @param iface interface name
 * @param mode either 'start' or 'stop'
 * @param phase script phase, i.e. 'pre-up', 'post-up', 'pre-down', 'post-down'
 * @param iap_id Unique IAP identifier, currently the escaped iap name
 * @param iap_type IAP type
 * @param remove_proxies TRUE to set ICD_PROXY_UNSET=1
 * @param env rest of the environment variables
 *
 * @return pid of the child process or -1 on error
 *
 */
static pid_t
icd_script_exec (const gchar * script, const gchar *iface, const gchar *mode,
                 const gchar *phase, const gchar *iap_id, const gchar *iap_type,
                 const gboolean remove_proxies, const struct icd_iap_env *env)
{
  gchar *path = g_strdup_printf("/etc/network/if-%s.d", mode);
  pid_t pid = fork();

  if (pid == -1)
  {
    ILOG_ERR("forking of %s script failed", mode);
    g_free(path);
    return pid;
  }

  if (!pid)
  {
    ILOG_DEBUG("running %s scripts in %s, setting following env:", mode, path);

    if (iface)
    {
      SETENV(SCRIPT_IFACE, iface);
      SETENV(SCRIPT_LOGICAL, iface);
    }

    if (remove_proxies)
      SETENV(SCRIPT_PROXY_UNSET, "1");

    if (env && env->addrfam)
      SETENV(SCRIPT_ADDRFAM, env->addrfam);

    if (script)
      SETENV(SCRIPT_MODE, script);

    if (phase)
      SETENV(SCRIPT_PHASE, phase);

    SETENV(SCRIPT_VERBOSITY, SCRIPT_VERBOSITY_VALUE);
    SETENV(SCRIPT_PATH,
           "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");

    if (iap_id)
      SETENV(SCRIPT_IAP_ID, iap_id);

    if (iap_type)
      SETENV(SCRIPT_IAP_TYPE, iap_type);

    if (env)
    {
      GSList *l;

      for (l = env->envlist; l; l = l->next)
      {
        char *envs = (char *)l->data;

        if (envs)
        {
          ILOG_DEBUG("%s", envs);
          putenv(envs);
        }
      }
    }

    execl("/bin/run-parts", "/bin/run-parts", path, NULL);
    exit(1);
  }

  g_free(path);

  return pid;
}

/**
 * @brief Run network script and start waiting for the result
 *
 * @param script the script
 * @param iface interface name
 * @param mode mode
 * @param phase script phase, i.e. 'pre-up', 'post-up', 'pre-down', 'post-down'
 * @param iap_id Unique IAP identifier, currently the escaped iap name
 * @param iap_type IAP type
 * @param remove_proxies wheter to remove http, etc. proxies
 * @param env rest of the environment variables
 * @param cb callback function
 * @param user_data 	user data for callback function
 *
 * @return pid of child process or -1 on error
 *
 */
static pid_t
icd_script_run (const gchar *script, const gchar *iface, const gchar *mode,
                const gchar *phase, const gchar *iap_id, const gchar *iap_type,
                gboolean remove_proxies, const struct icd_iap_env *env,
                icd_script_cb_fn cb, gpointer user_data)
{
  pid_t pid;
  GSList **scripts;

  pid = icd_script_exec(script, iface, mode, phase,iap_id, iap_type,
                        remove_proxies,env);

  if (pid != -1)
  {
    struct icd_script_data *script_data = g_new0(struct icd_script_data, 1);
    pid_t *ppid;

    script_data->pid = pid;
    script_data->cb = cb;
    script_data->user_data = user_data;

    scripts = icd_script_get();
    *scripts = g_slist_prepend(*scripts, script_data);

    ppid = g_new(pid_t, 1);
    *ppid = pid;
    script_data->timeout_id = g_timeout_add(1000 * icd_script_timeout_secs(),
                                            icd_script_timeout, ppid);
  }

  return pid;
}

/**
 * @brief Run pre-up scripts
 *
 * @param iap_id Unique IAP identifier, currently the escaped iap name
 * @param iap_type IAP type
 * @param env script environment variables
 * @param cb callback
 * @param user_data user data for the callback
 *
 * @return the process id of the running script, -1 on error whereby the
 * callback will not be called
 *
 */
pid_t
icd_script_pre_up(const gchar *iap_id, const gchar *iap_type,
                  const struct icd_iap_env *env, icd_script_cb_fn cb,
                  gpointer user_data)
{
  return icd_script_run("start", NULL, "pre-up", "pre-up", iap_id, iap_type,
                        FALSE, env, cb, user_data);
}

/**
 * @brief Run post-up scripts
 *
 * @param iface interface name
 * @param iap_id Unique IAP identifier, currently the escaped iap name
 * @param iap_type IAP type
 * @param env script environment variables
 * @param cb callback
 * @param user_data user data for the callback
 *
 * @return the process id of the running script, -1 on error whereby the
 * callback will not be called
 *
 */
pid_t
icd_script_post_up(const gchar *iface, const gchar *iap_id,
                   const gchar *iap_type, const struct icd_iap_env *env,
                   icd_script_cb_fn cb, gpointer user_data)
{
  return icd_script_run("start", iface, "up", "post-up", iap_id, iap_type,
                        FALSE, env, cb, user_data);
}

/**
 * @brief Run pre-down scripts
 *
 * @param iface interface name
 * @param iap_id Unique IAP identifier, currently the escaped iap name
 * @param iap_type IAP type
 * @param remove_proxies remove http, etc. proxies if TRUE
 * @param env script environment variables
 * @param cb callback
 * @param user_data user data for the callback
 *
 * @return the process id of the running script, -1 on error whereby the
 * callback will not be called
 *
 */
pid_t
icd_script_pre_down(const gchar *iface, const gchar *iap_id,
                    const gchar *iap_type, gboolean remove_proxies,
                    const struct icd_iap_env *env, icd_script_cb_fn cb,
                    gpointer user_data)
{
  return icd_script_run("stop", iface, "down", "pre-down", iap_id, iap_type,
                        remove_proxies, env, cb, user_data);
}

/**
 * @brief Run post-down scripts
 *
 * @param iface interface name
 * @param iap_id Unique IAP identifier, currently the escaped iap name
 * @param iap_type IAP type
 * @param env script environment variables
 * @param cb callback
 * @param user_data user data for the callback
 *
 * @return the process id of the running script, -1 on error whereby the
 * callback will not be called
 *
 */
pid_t
icd_script_post_down(const gchar *iface, const gchar *iap_id,
                     const gchar *iap_type, const struct icd_iap_env *env,
                     icd_script_cb_fn cb, gpointer user_data)
{
  return icd_script_run("stop", iface, "post-down", "post-down", iap_id,
                        iap_type, FALSE, env, cb, user_data);
}

/**
 * @brief Cancel a running script
 *
 * @param pid script process id
 *
 */
void
icd_script_cancel(const pid_t pid)
{
  GSList **scripts = icd_script_get();
  GSList *script = *scripts;

  while (script)
  {
    GSList *next = script->next;
    struct icd_script_data *script_data =
        (struct icd_script_data *)script->data;

    if (script->data && script_data->pid == pid)
    {
      ILOG_INFO("script pid %d timeout id %d cancelled", script_data->pid,
                script_data->timeout_id);

      g_source_remove(script_data->timeout_id);
      kill(script_data->pid, SIGKILL);
      g_free(script_data);
      *scripts = g_slist_delete_link(*scripts, script);
    }

    script = next;
  }
}