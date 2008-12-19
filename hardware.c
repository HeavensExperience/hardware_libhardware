/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hardware/hardware.h>

#include <cutils/properties.h>

#include <dlfcn.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

#define LOG_TAG "HAL"
#include <utils/Log.h>

/** Base path of the hal modules */
#define HAL_LIBRARY_PATH "/system/lib/hw"

/**
 * There are a set of variant filename
 * for modules. The form of the filename
 * is "<MODULE_ID>.variant.so" so for the
 * led module the Dream variants of base are
 * "ro.product.board" and "ro.arch" would be:
 *
 * led.trout.so
 * led.ARMV6.so
 * led.default.so
 */
#define HAL_DEFAULT_VARIANT "default"
#define HAL_VARIANT_KEYS_COUNT 3
static const char *variant_keys[HAL_VARIANT_KEYS_COUNT] = {
    "ro.product.board",
    "ro.arch",
    HAL_DEFAULT_VARIANT
};

/**
 * Load the file defined by the path and if succesfull
 * return the dlopen handle and the hmi.
 * @return 0 = success, !0 = failure.
 */
static int load(const char *id,
                const char *path,
                void **pHandle,
                const struct hw_module_t **pHmi)
{
    int status;
    void *handle;
    const struct hw_module_t *hmi;

    LOGV("load: E id=%s path=%s", id, path);

    /*
     * load the symbols resolving undefined symbols before
     * dlopen returns. Since RTLD_GLOBAL is not or'd in with
     * RTLD_NOW the external symbols will not be global
     */
    handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
        char const *err_str = dlerror();
        LOGE("load: module=%s error=%s", path, err_str);
        status = -EINVAL;
        goto done;
    }

    /* Get the address of the struct hal_module_info. */
    const char *sym = HAL_MODULE_INFO_SYM_AS_STR;
    hmi = (const struct hw_module_t *)dlsym(handle, sym);
    if (hmi == NULL) {
        char const *err_str = dlerror();
        LOGE("load: couldn't find symbol %s", sym);
        status = -EINVAL;
        goto done;
    }

    /* Check that the id matches */
    if (strcmp(id, hmi->id) != 0) {
        LOGE("load: id=%s != hmi->id=%s", id, hmi->id);
        status = -EINVAL;
        goto done;
    }

    /* success */
    status = 0;

done:
    if (status != 0) {
        hmi = NULL;
        if (handle != NULL) {
            dlclose(handle);
            handle = NULL;
        }
    }

    *pHmi = hmi;
    *pHandle = handle;

    LOGV("load: X id=%s path=%s hmi=%p pHandle=%p status=%d",
         id, path, *pHmi, *pHandle, status);
    return status;
}

int hw_get_module(const char *id, const struct hw_module_t **module) 
{
    int status;
    const struct hw_module_t *hmi = NULL;
    char path[PATH_MAX];
    char variant[PATH_MAX];
    void *handle = NULL;
    int i;

    /*
     * Here we rely on the fact that calling dlopen multiple times on
     * the same .so will simply increment a refcount (and not load
     * a new copy of the library).
     * We also assume that dlopen() is thread-safe.
     */
    
    LOGV("hal_module_info_get: Load module id=%s", id);

    /* Loop through the configuration variants looking for a module */
    status = -EINVAL;
    for (i = 0; (status != 0) && (i < HAL_VARIANT_KEYS_COUNT); i++) {

        /* Get variant or default */
        if (strcmp(variant_keys[i], HAL_DEFAULT_VARIANT) == 0) {
            strncpy(variant, HAL_DEFAULT_VARIANT, sizeof(variant)-1);
            variant[sizeof(variant)-1] = 0;
        } else {
            if (property_get(variant_keys[i], variant, NULL) == 0) {
                continue;
            }
        }

        /* Construct the path then try to load */
        snprintf(path, sizeof(path), "%s/%s.%s.so",
                HAL_LIBRARY_PATH, id, variant);
        status = load(id, path, &handle, &hmi);
    }
    if (status != 0) {
        hmi = NULL;
        if (handle != NULL) {
            dlclose(handle);
        }
    }
    
    *module = hmi;
    LOGV("hal_module_info_get: X id=%s hmi=%p status=%d", id, hmi, status);

    return status;
}
