#pragma once
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
/* No device-side assembly assumptions; Tegra 4 is ARMv7, not ARMv8 crypto. */
#if defined(__arm__)
#undef MBEDTLS_AESCE_C
#undef MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_IF_PRESENT
#undef MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_ONLY
#endif
