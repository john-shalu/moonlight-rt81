#pragma once
#include <windows.h>
#include <d3d11.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct VideoCalls { LONG created, begun, submitted, ended; } VideoCalls;
BOOL install_video_hooks(ID3D11VideoDevice*, ID3D11VideoContext*);
void remove_video_hooks(void);
void reset_video_calls(void);
VideoCalls read_video_calls(void);
#ifdef __cplusplus
}
#endif
