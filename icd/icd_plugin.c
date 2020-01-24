/**
@file icd_plugin.c
@copyright GNU GPLv2 or later

@addtogroup icd_plugin Support functions for loadable plugins
@ingroup internal

 * @{ */

#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include "icd_plugin.h"
#include "icd_log.h"

/**
 * Load the plugin shared object file
 *
 * @param filename   full pathname to the shared object file
 * @param name       name of the module
 * @param init_name  the name of the plugin initialization function
 * @param cb         function called when the plugin has been loaded
 * @param cb_data    data to pass to the callback
 *
 * @return  TRUE on success, FALSE on failure
 */
gboolean
icd_plugin_load(const char *filename, const char *name, const char *init_name,
                icd_plugin_load_cb_fn cb, gpointer cb_data)
{
  void *handle;
  void *init_function;

  if (!cb)
  {
    ILOG_ERR("No callback function for plugin loading");
    return FALSE;
  }

  handle = dlopen(filename, RTLD_NOW);

  if (!handle)
  {
    ILOG_ERR("Failed to load plugin %s: %s.", filename, dlerror());
    return FALSE;
  }

  init_function = dlsym(handle, init_name);

  if (init_function)
  {
    ILOG_DEBUG("Loading plugin %s", filename);

    if (cb(name, handle, init_function, cb_data))
      return TRUE;

    ILOG_DEBUG("module %s init failed, unloading", filename);
  }
  else
    ILOG_ERR("No plugin init function in %s: %s.", filename, dlerror());

  dlclose(handle);

  return FALSE;
}

/**
 * Unload a module.
 * @param handle  module handle given to module initialization callback
 */
void
icd_plugin_unload_module(void *handle)
{
  dlclose(handle);
}

/**
 * Load all plugins from a specified list of file names.
 *
 * @param plugindir    path for the plugins
 * @param plugin_list  list of plugin file names
 * @param init_name    the name of the plugin initialization function
 * @param cb           function called when the plugin has been loaded
 * @param cb_data      data to pass to the callback
 *
 * @return  TRUE if loading of at least one plugin loading succeeded, FALSE
 *          if no modules were loaded
 */
gboolean
icd_plugin_load_list(const char *plugindir, GSList *plugin_list,
                     const char *init_name, icd_plugin_load_cb_fn cb,
                     gpointer cb_data)
{
  GSList *l;
  gboolean loaded = FALSE;

  if (!plugin_list)
  {
    ILOG_WARN("Plugin list is empty");
    return FALSE;
  }

  l = plugin_list;
  loaded = 0;

  for (l = plugin_list; l; l = l->next)
  {
    gchar *filename;
    const char *libname = (const char *)l->data;

    if (!libname)
    {
      ILOG_ERR("Plugin list contains NULL module name");
      continue;
    }

    if (!g_str_has_suffix(libname, ".so"))
    {
      ILOG_ERR("Plugin name does not end with .so");
      continue;
    }

    filename = g_strconcat(plugindir, "/", libname, NULL);

    if (icd_plugin_load(filename, libname, init_name, cb, cb_data))
      loaded = TRUE;

    g_free(filename);
  }

  if (!loaded)
    ILOG_WARN("No listed modules loaded from '%s'", plugindir);

  return loaded;
}

/**
 * Load all plugins in specified directory. Enumerates all files in the
 * directory and tries to load all shared objects in the there.
 *
 * @param plugindir  path for the plugins
 * @param prefix     prefix for the plugins to load
 * @param init_name  the name of the plugin initialization function
 * @param cb         function called when the plugin has been loaded
 * @param cb_data    data to pass to the callback
 *
 * @return  TRUE if loading of at least one plugin loading succeeded, FALSE
 *          if no modules were loaded
 */
gboolean
icd_plugin_load_all(const char *plugindir, const char *prefix,
                    const char *init_name, icd_plugin_load_cb_fn cb,
                    gpointer cb_data)
{
  DIR *dir = opendir(plugindir);
  struct dirent *dent;
  gboolean loaded = FALSE;

  if (!dir)
  {
    ILOG_WARN("Failed to open plugin directory %s", plugindir);
    return FALSE;
  }

  for (dent = readdir(dir); dent; dent = readdir(dir))
  {
    const gchar *name = dent->d_name;
    gchar *filename;

    if (!g_str_has_suffix(dent->d_name, ".so") ||
        !g_strstr_len(name, strlen(prefix), prefix))
    {
        continue;
    }

    filename = g_strconcat(plugindir, "/", name, NULL);

    if (icd_plugin_load(filename, name, init_name, cb, cb_data))
      loaded = TRUE;

    g_free(filename);
  }

  if (!loaded)
    ILOG_WARN("No '%s*' modules loaded", prefix);

  closedir(dir);

  return loaded;
}

/** @} */
