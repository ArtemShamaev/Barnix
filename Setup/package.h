#ifndef BARNIX_SETUP_PACKAGE_H
#define BARNIX_SETUP_PACKAGE_H
/* Installs one BPK1 archive from an HTTP repository into the selected ext2
 * filesystem. expected is a 64-character SHA-256 digest from the ISO. */
typedef void (*SetupPackageProgress)(unsigned int processed, unsigned int total, void *context);
int setup_install_package(const char *url, const char *expected);
int setup_install_package_memory(const void *data, unsigned int size, const char *expected,
								 SetupPackageProgress progress, void *context);
#endif
