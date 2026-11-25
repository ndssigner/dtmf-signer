/* libsecp256k1's default callbacks, supplied here instead of the library's
 * (SECP256K1_USE_EXTERNAL_DEFAULT_CALLBACKS): those print with fprintf,
 * which links newlib's whole printf with floats, and that code carries a
 * path of the toolchain's build machine that differs between Arm's Linux and
 * macOS packages, so the UF2 would differ by the host it was built on. They
 * are called only for a bug (an argument the library rejects): stop. */
#include <stdlib.h>

void secp256k1_default_illegal_callback_fn(const char *message, void *data);
void secp256k1_default_error_callback_fn(const char *message, void *data);

void secp256k1_default_illegal_callback_fn(const char *message, void *data)
{
    (void)message;
    (void)data;
    abort();
}

void secp256k1_default_error_callback_fn(const char *message, void *data)
{
    (void)message;
    (void)data;
    abort();
}
