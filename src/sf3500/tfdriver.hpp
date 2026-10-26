/* Device settings from TreeFrogUI.
 *
 * TreeFrogUI writes /tmp/tfdevice.env at boot describing the device it runs on
 * (TF_DEVICE, TF_PANEL_W/H, TF_ROTATE, TF_PRESENT, and TF_DRIVER, the driver
 * that matches the hardware, e.g. /mnt/sdcard/cubegm/driver_sf3000.so). Look
 * keys up there so the same binary works on every supported device. A variable
 * of the same name in the environment wins. */
#ifndef TFDRIVER_HPP
#define TFDRIVER_HPP

/* Value of KEY from the environment or tfdevice.env, or NULL if not set. */
const char *tfenv_get(const char *key);

/* Driver to dlopen: TF_DRIVER, else the SF3500 driver this port was developed on. */
const char *tfdriver_path(void);

#endif
