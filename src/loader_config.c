/* Subsystem 6.3+ images require a loader-visible security-cookie record even
 * when using a freestanding entry point. Windows initializes this cookie. */
#include <windows.h>
#ifdef _WIN64
ULONG_PTR __security_cookie=0x00002b992ddfa232ULL;
#else
ULONG_PTR __security_cookie=0xbb40e64e;
#endif
const IMAGE_LOAD_CONFIG_DIRECTORY _load_config_used={
 .Size=sizeof(IMAGE_LOAD_CONFIG_DIRECTORY),
 .SecurityCookie=(ULONG_PTR)&__security_cookie
};
/* Let the OS initialize the same cookie used by Clang's GNU stack protector. */
extern ULONG_PTR __stack_chk_guard __attribute__((alias("__security_cookie")));
