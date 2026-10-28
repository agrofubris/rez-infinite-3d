// Auto-generated forwarding interface objects (C++ vtable ABI).
// Each method forwards to the corresponding FnTable entry.

struct ImplIVRSystem015 {
    virtual void GetRecommendedRenderTargetSize(uint32_t * pnWidth, uint32_t * pnHeight) { g_sys015.GetRecommendedRenderTargetSize(pnWidth, pnHeight); }
    virtual struct HmdMatrix44_t GetProjectionMatrix(EVREye eEye, float fNearZ, float fFarZ) { return g_sys015.GetProjectionMatrix(eEye, fNearZ, fFarZ); }
    virtual void GetProjectionRaw(EVREye eEye, float * pfLeft, float * pfRight, float * pfTop, float * pfBottom) { g_sys015.GetProjectionRaw(eEye, pfLeft, pfRight, pfTop, pfBottom); }
    virtual bool ComputeDistortion(EVREye eEye, float fU, float fV, struct DistortionCoordinates_t * pDistortionCoordinates) { return g_sys015.ComputeDistortion(eEye, fU, fV, pDistortionCoordinates); }
    virtual struct HmdMatrix34_t GetEyeToHeadTransform(EVREye eEye) { return g_sys015.GetEyeToHeadTransform(eEye); }
    virtual bool GetTimeSinceLastVsync(float * pfSecondsSinceLastVsync, uint64_t * pulFrameCounter) { return g_sys015.GetTimeSinceLastVsync(pfSecondsSinceLastVsync, pulFrameCounter); }
    virtual int32_t GetD3D9AdapterIndex() { return g_sys015.GetD3D9AdapterIndex(); }
    virtual void GetDXGIOutputInfo(int32_t * pnAdapterIndex) { g_sys015.GetDXGIOutputInfo(pnAdapterIndex); }
    virtual bool IsDisplayOnDesktop() { return g_sys015.IsDisplayOnDesktop(); }
    virtual bool SetDisplayVisibility(bool bIsVisibleOnDesktop) { return g_sys015.SetDisplayVisibility(bIsVisibleOnDesktop); }
    virtual void GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow, struct TrackedDevicePose_t * pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount) { g_sys015.GetDeviceToAbsoluteTrackingPose(eOrigin, fPredictedSecondsToPhotonsFromNow, pTrackedDevicePoseArray, unTrackedDevicePoseArrayCount); }
    virtual void ResetSeatedZeroPose() { g_sys015.ResetSeatedZeroPose(); }
    virtual struct HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() { return g_sys015.GetSeatedZeroPoseToStandingAbsoluteTrackingPose(); }
    virtual struct HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() { return g_sys015.GetRawZeroPoseToStandingAbsoluteTrackingPose(); }
    virtual uint32_t GetSortedTrackedDeviceIndicesOfClass(ETrackedDeviceClass eTrackedDeviceClass, TrackedDeviceIndex_t * punTrackedDeviceIndexArray, uint32_t unTrackedDeviceIndexArrayCount, TrackedDeviceIndex_t unRelativeToTrackedDeviceIndex) { return g_sys015.GetSortedTrackedDeviceIndicesOfClass(eTrackedDeviceClass, punTrackedDeviceIndexArray, unTrackedDeviceIndexArrayCount, unRelativeToTrackedDeviceIndex); }
    virtual EDeviceActivityLevel GetTrackedDeviceActivityLevel(TrackedDeviceIndex_t unDeviceId) { return g_sys015.GetTrackedDeviceActivityLevel(unDeviceId); }
    virtual void ApplyTransform(struct TrackedDevicePose_t * pOutputPose, struct TrackedDevicePose_t * pTrackedDevicePose, struct HmdMatrix34_t * pTransform) { g_sys015.ApplyTransform(pOutputPose, pTrackedDevicePose, pTransform); }
    virtual TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole unDeviceType) { return g_sys015.GetTrackedDeviceIndexForControllerRole(unDeviceType); }
    virtual ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(TrackedDeviceIndex_t unDeviceIndex) { return g_sys015.GetControllerRoleForTrackedDeviceIndex(unDeviceIndex); }
    virtual ETrackedDeviceClass GetTrackedDeviceClass(TrackedDeviceIndex_t unDeviceIndex) { return g_sys015.GetTrackedDeviceClass(unDeviceIndex); }
    virtual bool IsTrackedDeviceConnected(TrackedDeviceIndex_t unDeviceIndex) { return g_sys015.IsTrackedDeviceConnected(unDeviceIndex); }
    virtual bool GetBoolTrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys015.GetBoolTrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual float GetFloatTrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys015.GetFloatTrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual int32_t GetInt32TrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys015.GetInt32TrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual uint64_t GetUint64TrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys015.GetUint64TrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual struct HmdMatrix34_t GetMatrix34TrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys015.GetMatrix34TrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual uint32_t GetStringTrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, char * pchValue, uint32_t unBufferSize, ETrackedPropertyError * pError) { return g_sys015.GetStringTrackedDeviceProperty(unDeviceIndex, prop, pchValue, unBufferSize, pError); }
    virtual char * GetPropErrorNameFromEnum(ETrackedPropertyError error) { return g_sys015.GetPropErrorNameFromEnum(error); }
    virtual bool PollNextEvent(struct VREvent_t * pEvent, uint32_t uncbVREvent) { return g_sys015.PollNextEvent(pEvent, uncbVREvent); }
    virtual bool PollNextEventWithPose(ETrackingUniverseOrigin eOrigin, struct VREvent_t * pEvent, uint32_t uncbVREvent, TrackedDevicePose_t * pTrackedDevicePose) { return g_sys015.PollNextEventWithPose(eOrigin, pEvent, uncbVREvent, pTrackedDevicePose); }
    virtual char * GetEventTypeNameFromEnum(EVREventType eType) { return g_sys015.GetEventTypeNameFromEnum(eType); }
    virtual struct HiddenAreaMesh_t GetHiddenAreaMesh(EVREye eEye, EHiddenAreaMeshType type) { return g_sys015.GetHiddenAreaMesh(eEye, type); }
    virtual bool GetControllerState(TrackedDeviceIndex_t unControllerDeviceIndex, VRControllerState_t * pControllerState, uint32_t unControllerStateSize) { return g_sys015.GetControllerState(unControllerDeviceIndex, pControllerState, unControllerStateSize); }
    virtual bool GetControllerStateWithPose(ETrackingUniverseOrigin eOrigin, TrackedDeviceIndex_t unControllerDeviceIndex, VRControllerState_t * pControllerState, uint32_t unControllerStateSize, struct TrackedDevicePose_t * pTrackedDevicePose) { return g_sys015.GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, unControllerStateSize, pTrackedDevicePose); }
    virtual void TriggerHapticPulse(TrackedDeviceIndex_t unControllerDeviceIndex, uint32_t unAxisId, unsigned short usDurationMicroSec) { g_sys015.TriggerHapticPulse(unControllerDeviceIndex, unAxisId, usDurationMicroSec); }
    virtual char * GetButtonIdNameFromEnum(EVRButtonId eButtonId) { return g_sys015.GetButtonIdNameFromEnum(eButtonId); }
    virtual char * GetControllerAxisTypeNameFromEnum(EVRControllerAxisType eAxisType) { return g_sys015.GetControllerAxisTypeNameFromEnum(eAxisType); }
    virtual bool CaptureInputFocus() { return g_sys015.CaptureInputFocus(); }
    virtual void ReleaseInputFocus() { g_sys015.ReleaseInputFocus(); }
    virtual bool IsInputFocusCapturedByAnotherProcess() { return g_sys015.IsInputFocusCapturedByAnotherProcess(); }
    virtual uint32_t DriverDebugRequest(TrackedDeviceIndex_t unDeviceIndex, char * pchRequest, char * pchResponseBuffer, uint32_t unResponseBufferSize) { return g_sys015.DriverDebugRequest(unDeviceIndex, pchRequest, pchResponseBuffer, unResponseBufferSize); }
    virtual EVRFirmwareError PerformFirmwareUpdate(TrackedDeviceIndex_t unDeviceIndex) { return g_sys015.PerformFirmwareUpdate(unDeviceIndex); }
    virtual void AcknowledgeQuit_Exiting() { g_sys015.AcknowledgeQuit_Exiting(); }
    virtual void AcknowledgeQuit_UserPrompt() { g_sys015.AcknowledgeQuit_UserPrompt(); }
};
static ImplIVRSystem015 g_impl_g_sys015;

struct ImplIVRSystem012 {
    virtual void GetRecommendedRenderTargetSize(uint32_t * pnWidth, uint32_t * pnHeight) { g_sys012.GetRecommendedRenderTargetSize(pnWidth, pnHeight); }
    virtual struct HmdMatrix44_t GetProjectionMatrix(EVREye eEye, float fNearZ, float fFarZ, EGraphicsAPIConvention eProjType) { return g_sys012.GetProjectionMatrix(eEye, fNearZ, fFarZ, eProjType); }
    virtual void GetProjectionRaw(EVREye eEye, float * pfLeft, float * pfRight, float * pfTop, float * pfBottom) { g_sys012.GetProjectionRaw(eEye, pfLeft, pfRight, pfTop, pfBottom); }
    virtual struct DistortionCoordinates_t ComputeDistortion(EVREye eEye, float fU, float fV) { return g_sys012.ComputeDistortion(eEye, fU, fV); }
    virtual struct HmdMatrix34_t GetEyeToHeadTransform(EVREye eEye) { return g_sys012.GetEyeToHeadTransform(eEye); }
    virtual bool GetTimeSinceLastVsync(float * pfSecondsSinceLastVsync, uint64_t * pulFrameCounter) { return g_sys012.GetTimeSinceLastVsync(pfSecondsSinceLastVsync, pulFrameCounter); }
    virtual int32_t GetD3D9AdapterIndex() { return g_sys012.GetD3D9AdapterIndex(); }
    virtual void GetDXGIOutputInfo(int32_t * pnAdapterIndex) { g_sys012.GetDXGIOutputInfo(pnAdapterIndex); }
    virtual bool IsDisplayOnDesktop() { return g_sys012.IsDisplayOnDesktop(); }
    virtual bool SetDisplayVisibility(bool bIsVisibleOnDesktop) { return g_sys012.SetDisplayVisibility(bIsVisibleOnDesktop); }
    virtual void GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow, struct TrackedDevicePose_t * pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount) { g_sys012.GetDeviceToAbsoluteTrackingPose(eOrigin, fPredictedSecondsToPhotonsFromNow, pTrackedDevicePoseArray, unTrackedDevicePoseArrayCount); }
    virtual void ResetSeatedZeroPose() { g_sys012.ResetSeatedZeroPose(); }
    virtual struct HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() { return g_sys012.GetSeatedZeroPoseToStandingAbsoluteTrackingPose(); }
    virtual struct HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() { return g_sys012.GetRawZeroPoseToStandingAbsoluteTrackingPose(); }
    virtual uint32_t GetSortedTrackedDeviceIndicesOfClass(ETrackedDeviceClass eTrackedDeviceClass, TrackedDeviceIndex_t * punTrackedDeviceIndexArray, uint32_t unTrackedDeviceIndexArrayCount, TrackedDeviceIndex_t unRelativeToTrackedDeviceIndex) { return g_sys012.GetSortedTrackedDeviceIndicesOfClass(eTrackedDeviceClass, punTrackedDeviceIndexArray, unTrackedDeviceIndexArrayCount, unRelativeToTrackedDeviceIndex); }
    virtual EDeviceActivityLevel GetTrackedDeviceActivityLevel(TrackedDeviceIndex_t unDeviceId) { return g_sys012.GetTrackedDeviceActivityLevel(unDeviceId); }
    virtual void ApplyTransform(struct TrackedDevicePose_t * pOutputPose, struct TrackedDevicePose_t * pTrackedDevicePose, struct HmdMatrix34_t * pTransform) { g_sys012.ApplyTransform(pOutputPose, pTrackedDevicePose, pTransform); }
    virtual TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole unDeviceType) { return g_sys012.GetTrackedDeviceIndexForControllerRole(unDeviceType); }
    virtual ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(TrackedDeviceIndex_t unDeviceIndex) { return g_sys012.GetControllerRoleForTrackedDeviceIndex(unDeviceIndex); }
    virtual ETrackedDeviceClass GetTrackedDeviceClass(TrackedDeviceIndex_t unDeviceIndex) { return g_sys012.GetTrackedDeviceClass(unDeviceIndex); }
    virtual bool IsTrackedDeviceConnected(TrackedDeviceIndex_t unDeviceIndex) { return g_sys012.IsTrackedDeviceConnected(unDeviceIndex); }
    virtual bool GetBoolTrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys012.GetBoolTrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual float GetFloatTrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys012.GetFloatTrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual int32_t GetInt32TrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys012.GetInt32TrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual uint64_t GetUint64TrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys012.GetUint64TrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual struct HmdMatrix34_t GetMatrix34TrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError * pError) { return g_sys012.GetMatrix34TrackedDeviceProperty(unDeviceIndex, prop, pError); }
    virtual uint32_t GetStringTrackedDeviceProperty(TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, char * pchValue, uint32_t unBufferSize, ETrackedPropertyError * pError) { return g_sys012.GetStringTrackedDeviceProperty(unDeviceIndex, prop, pchValue, unBufferSize, pError); }
    virtual char * GetPropErrorNameFromEnum(ETrackedPropertyError error) { return g_sys012.GetPropErrorNameFromEnum(error); }
    virtual bool PollNextEvent(struct VREvent_t * pEvent, uint32_t uncbVREvent) { return g_sys012.PollNextEvent(pEvent, uncbVREvent); }
    virtual bool PollNextEventWithPose(ETrackingUniverseOrigin eOrigin, struct VREvent_t * pEvent, uint32_t uncbVREvent, TrackedDevicePose_t * pTrackedDevicePose) { return g_sys012.PollNextEventWithPose(eOrigin, pEvent, uncbVREvent, pTrackedDevicePose); }
    virtual char * GetEventTypeNameFromEnum(EVREventType eType) { return g_sys012.GetEventTypeNameFromEnum(eType); }
    virtual struct HiddenAreaMesh_t GetHiddenAreaMesh(EVREye eEye) { return g_sys012.GetHiddenAreaMesh(eEye); }
    virtual bool GetControllerState(TrackedDeviceIndex_t unControllerDeviceIndex, VRControllerState_t * pControllerState) { return g_sys012.GetControllerState(unControllerDeviceIndex, pControllerState); }
    virtual bool GetControllerStateWithPose(ETrackingUniverseOrigin eOrigin, TrackedDeviceIndex_t unControllerDeviceIndex, VRControllerState_t * pControllerState, struct TrackedDevicePose_t * pTrackedDevicePose) { return g_sys012.GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState, pTrackedDevicePose); }
    virtual void TriggerHapticPulse(TrackedDeviceIndex_t unControllerDeviceIndex, uint32_t unAxisId, unsigned short usDurationMicroSec) { g_sys012.TriggerHapticPulse(unControllerDeviceIndex, unAxisId, usDurationMicroSec); }
    virtual char * GetButtonIdNameFromEnum(EVRButtonId eButtonId) { return g_sys012.GetButtonIdNameFromEnum(eButtonId); }
    virtual char * GetControllerAxisTypeNameFromEnum(EVRControllerAxisType eAxisType) { return g_sys012.GetControllerAxisTypeNameFromEnum(eAxisType); }
    virtual bool CaptureInputFocus() { return g_sys012.CaptureInputFocus(); }
    virtual void ReleaseInputFocus() { g_sys012.ReleaseInputFocus(); }
    virtual bool IsInputFocusCapturedByAnotherProcess() { return g_sys012.IsInputFocusCapturedByAnotherProcess(); }
    virtual uint32_t DriverDebugRequest(TrackedDeviceIndex_t unDeviceIndex, char * pchRequest, char * pchResponseBuffer, uint32_t unResponseBufferSize) { return g_sys012.DriverDebugRequest(unDeviceIndex, pchRequest, pchResponseBuffer, unResponseBufferSize); }
    virtual EVRFirmwareError PerformFirmwareUpdate(TrackedDeviceIndex_t unDeviceIndex) { return g_sys012.PerformFirmwareUpdate(unDeviceIndex); }
    virtual void AcknowledgeQuit_Exiting() { g_sys012.AcknowledgeQuit_Exiting(); }
    virtual void AcknowledgeQuit_UserPrompt() { g_sys012.AcknowledgeQuit_UserPrompt(); }
};
static ImplIVRSystem012 g_impl_g_sys012;

struct ImplIVRCompositor015 {
    virtual void SetTrackingSpace(ETrackingUniverseOrigin eOrigin) { g_comp015.SetTrackingSpace(eOrigin); }
    virtual ETrackingUniverseOrigin GetTrackingSpace() { return g_comp015.GetTrackingSpace(); }
    virtual EVRCompositorError WaitGetPoses(struct TrackedDevicePose_t * pRenderPoseArray, uint32_t unRenderPoseArrayCount, struct TrackedDevicePose_t * pGamePoseArray, uint32_t unGamePoseArrayCount) { return g_comp015.WaitGetPoses(pRenderPoseArray, unRenderPoseArrayCount, pGamePoseArray, unGamePoseArrayCount); }
    virtual EVRCompositorError GetLastPoses(struct TrackedDevicePose_t * pRenderPoseArray, uint32_t unRenderPoseArrayCount, struct TrackedDevicePose_t * pGamePoseArray, uint32_t unGamePoseArrayCount) { return g_comp015.GetLastPoses(pRenderPoseArray, unRenderPoseArrayCount, pGamePoseArray, unGamePoseArrayCount); }
    virtual EVRCompositorError GetLastPoseForTrackedDeviceIndex(TrackedDeviceIndex_t unDeviceIndex, struct TrackedDevicePose_t * pOutputPose, struct TrackedDevicePose_t * pOutputGamePose) { return g_comp015.GetLastPoseForTrackedDeviceIndex(unDeviceIndex, pOutputPose, pOutputGamePose); }
    virtual EVRCompositorError Submit(EVREye eEye, struct Texture_t * pTexture, struct VRTextureBounds_t * pBounds, EVRSubmitFlags nSubmitFlags) { return g_comp015.Submit(eEye, pTexture, pBounds, nSubmitFlags); }
    virtual void ClearLastSubmittedFrame() { g_comp015.ClearLastSubmittedFrame(); }
    virtual void PostPresentHandoff() { g_comp015.PostPresentHandoff(); }
    virtual bool GetFrameTiming(struct Compositor_FrameTiming * pTiming, uint32_t unFramesAgo) { return g_comp015.GetFrameTiming(pTiming, unFramesAgo); }
    virtual float GetFrameTimeRemaining() { return g_comp015.GetFrameTimeRemaining(); }
    virtual void GetCumulativeStats(struct Compositor_CumulativeStats * pStats, uint32_t nStatsSizeInBytes) { g_comp015.GetCumulativeStats(pStats, nStatsSizeInBytes); }
    virtual void FadeToColor(float fSeconds, float fRed, float fGreen, float fBlue, float fAlpha, bool bBackground) { g_comp015.FadeToColor(fSeconds, fRed, fGreen, fBlue, fAlpha, bBackground); }
    virtual void FadeGrid(float fSeconds, bool bFadeIn) { g_comp015.FadeGrid(fSeconds, bFadeIn); }
    virtual EVRCompositorError SetSkyboxOverride(struct Texture_t * pTextures, uint32_t unTextureCount) { return g_comp015.SetSkyboxOverride(pTextures, unTextureCount); }
    virtual void ClearSkyboxOverride() { g_comp015.ClearSkyboxOverride(); }
    virtual void CompositorBringToFront() { g_comp015.CompositorBringToFront(); }
    virtual void CompositorGoToBack() { g_comp015.CompositorGoToBack(); }
    virtual void CompositorQuit() { g_comp015.CompositorQuit(); }
    virtual bool IsFullscreen() { return g_comp015.IsFullscreen(); }
    virtual uint32_t GetCurrentSceneFocusProcess() { return g_comp015.GetCurrentSceneFocusProcess(); }
    virtual uint32_t GetLastFrameRenderer() { return g_comp015.GetLastFrameRenderer(); }
    virtual bool CanRenderScene() { return g_comp015.CanRenderScene(); }
    virtual void ShowMirrorWindow() { g_comp015.ShowMirrorWindow(); }
    virtual void HideMirrorWindow() { g_comp015.HideMirrorWindow(); }
    virtual bool IsMirrorWindowVisible() { return g_comp015.IsMirrorWindowVisible(); }
    virtual void CompositorDumpImages() { g_comp015.CompositorDumpImages(); }
    virtual bool ShouldAppRenderWithLowResources() { return g_comp015.ShouldAppRenderWithLowResources(); }
    virtual void ForceInterleavedReprojectionOn(bool bOverride) { g_comp015.ForceInterleavedReprojectionOn(bOverride); }
    virtual void ForceReconnectProcess() { g_comp015.ForceReconnectProcess(); }
    virtual void SuspendRendering(bool bSuspend) { g_comp015.SuspendRendering(bSuspend); }
    virtual EVRCompositorError RequestScreenshot(EVRScreenshotType type, char * pchDestinationFileName, char * pchVRDestinationFileName) { return g_comp015.RequestScreenshot(type, pchDestinationFileName, pchVRDestinationFileName); }
    virtual EVRScreenshotType GetCurrentScreenshotType() { return g_comp015.GetCurrentScreenshotType(); }
    virtual EVRCompositorError GetMirrorTextureD3D11(EVREye eEye, void * pD3D11DeviceOrResource, void ** ppD3D11ShaderResourceView) { return g_comp015.GetMirrorTextureD3D11(eEye, pD3D11DeviceOrResource, ppD3D11ShaderResourceView); }
    virtual EVRCompositorError GetMirrorTextureGL(EVREye eEye, glUInt_t * pglTextureId, glSharedTextureHandle_t * pglSharedTextureHandle) { return g_comp015.GetMirrorTextureGL(eEye, pglTextureId, pglSharedTextureHandle); }
    virtual bool ReleaseSharedGLTexture(glUInt_t glTextureId, glSharedTextureHandle_t glSharedTextureHandle) { return g_comp015.ReleaseSharedGLTexture(glTextureId, glSharedTextureHandle); }
    virtual void LockGLSharedTextureForAccess(glSharedTextureHandle_t glSharedTextureHandle) { g_comp015.LockGLSharedTextureForAccess(glSharedTextureHandle); }
    virtual void UnlockGLSharedTextureForAccess(glSharedTextureHandle_t glSharedTextureHandle) { g_comp015.UnlockGLSharedTextureForAccess(glSharedTextureHandle); }
};
static ImplIVRCompositor015 g_impl_g_comp015;

struct ImplIVRCompositor016 {
    virtual void SetTrackingSpace(ETrackingUniverseOrigin eOrigin) { g_comp016.SetTrackingSpace(eOrigin); }
    virtual ETrackingUniverseOrigin GetTrackingSpace() { return g_comp016.GetTrackingSpace(); }
    virtual EVRCompositorError WaitGetPoses(struct TrackedDevicePose_t * pRenderPoseArray, uint32_t unRenderPoseArrayCount, struct TrackedDevicePose_t * pGamePoseArray, uint32_t unGamePoseArrayCount) { return g_comp016.WaitGetPoses(pRenderPoseArray, unRenderPoseArrayCount, pGamePoseArray, unGamePoseArrayCount); }
    virtual EVRCompositorError GetLastPoses(struct TrackedDevicePose_t * pRenderPoseArray, uint32_t unRenderPoseArrayCount, struct TrackedDevicePose_t * pGamePoseArray, uint32_t unGamePoseArrayCount) { return g_comp016.GetLastPoses(pRenderPoseArray, unRenderPoseArrayCount, pGamePoseArray, unGamePoseArrayCount); }
    virtual EVRCompositorError GetLastPoseForTrackedDeviceIndex(TrackedDeviceIndex_t unDeviceIndex, struct TrackedDevicePose_t * pOutputPose, struct TrackedDevicePose_t * pOutputGamePose) { return g_comp016.GetLastPoseForTrackedDeviceIndex(unDeviceIndex, pOutputPose, pOutputGamePose); }
    virtual EVRCompositorError Submit(EVREye eEye, struct Texture_t * pTexture, struct VRTextureBounds_t * pBounds, EVRSubmitFlags nSubmitFlags) { return g_comp016.Submit(eEye, pTexture, pBounds, nSubmitFlags); }
    virtual void ClearLastSubmittedFrame() { g_comp016.ClearLastSubmittedFrame(); }
    virtual void PostPresentHandoff() { g_comp016.PostPresentHandoff(); }
    virtual bool GetFrameTiming(struct Compositor_FrameTiming * pTiming, uint32_t unFramesAgo) { return g_comp016.GetFrameTiming(pTiming, unFramesAgo); }
    virtual float GetFrameTimeRemaining() { return g_comp016.GetFrameTimeRemaining(); }
    virtual void GetCumulativeStats(struct Compositor_CumulativeStats * pStats, uint32_t nStatsSizeInBytes) { g_comp016.GetCumulativeStats(pStats, nStatsSizeInBytes); }
    virtual void FadeToColor(float fSeconds, float fRed, float fGreen, float fBlue, float fAlpha, bool bBackground) { g_comp016.FadeToColor(fSeconds, fRed, fGreen, fBlue, fAlpha, bBackground); }
    virtual void FadeGrid(float fSeconds, bool bFadeIn) { g_comp016.FadeGrid(fSeconds, bFadeIn); }
    virtual EVRCompositorError SetSkyboxOverride(struct Texture_t * pTextures, uint32_t unTextureCount) { return g_comp016.SetSkyboxOverride(pTextures, unTextureCount); }
    virtual void ClearSkyboxOverride() { g_comp016.ClearSkyboxOverride(); }
    virtual void CompositorBringToFront() { g_comp016.CompositorBringToFront(); }
    virtual void CompositorGoToBack() { g_comp016.CompositorGoToBack(); }
    virtual void CompositorQuit() { g_comp016.CompositorQuit(); }
    virtual bool IsFullscreen() { return g_comp016.IsFullscreen(); }
    virtual uint32_t GetCurrentSceneFocusProcess() { return g_comp016.GetCurrentSceneFocusProcess(); }
    virtual uint32_t GetLastFrameRenderer() { return g_comp016.GetLastFrameRenderer(); }
    virtual bool CanRenderScene() { return g_comp016.CanRenderScene(); }
    virtual void ShowMirrorWindow() { g_comp016.ShowMirrorWindow(); }
    virtual void HideMirrorWindow() { g_comp016.HideMirrorWindow(); }
    virtual bool IsMirrorWindowVisible() { return g_comp016.IsMirrorWindowVisible(); }
    virtual void CompositorDumpImages() { g_comp016.CompositorDumpImages(); }
    virtual bool ShouldAppRenderWithLowResources() { return g_comp016.ShouldAppRenderWithLowResources(); }
    virtual void ForceInterleavedReprojectionOn(bool bOverride) { g_comp016.ForceInterleavedReprojectionOn(bOverride); }
    virtual void ForceReconnectProcess() { g_comp016.ForceReconnectProcess(); }
    virtual void SuspendRendering(bool bSuspend) { g_comp016.SuspendRendering(bSuspend); }
    virtual EVRCompositorError GetMirrorTextureD3D11(EVREye eEye, void * pD3D11DeviceOrResource, void ** ppD3D11ShaderResourceView) { return g_comp016.GetMirrorTextureD3D11(eEye, pD3D11DeviceOrResource, ppD3D11ShaderResourceView); }
    virtual EVRCompositorError GetMirrorTextureGL(EVREye eEye, glUInt_t * pglTextureId, glSharedTextureHandle_t * pglSharedTextureHandle) { return g_comp016.GetMirrorTextureGL(eEye, pglTextureId, pglSharedTextureHandle); }
    virtual bool ReleaseSharedGLTexture(glUInt_t glTextureId, glSharedTextureHandle_t glSharedTextureHandle) { return g_comp016.ReleaseSharedGLTexture(glTextureId, glSharedTextureHandle); }
    virtual void LockGLSharedTextureForAccess(glSharedTextureHandle_t glSharedTextureHandle) { g_comp016.LockGLSharedTextureForAccess(glSharedTextureHandle); }
    virtual void UnlockGLSharedTextureForAccess(glSharedTextureHandle_t glSharedTextureHandle) { g_comp016.UnlockGLSharedTextureForAccess(glSharedTextureHandle); }
};
static ImplIVRCompositor016 g_impl_g_comp016;

struct ImplIVRApplications006 {
    virtual EVRApplicationError AddApplicationManifest(char * pchApplicationManifestFullPath, bool bTemporary) { return g_apps.AddApplicationManifest(pchApplicationManifestFullPath, bTemporary); }
    virtual EVRApplicationError RemoveApplicationManifest(char * pchApplicationManifestFullPath) { return g_apps.RemoveApplicationManifest(pchApplicationManifestFullPath); }
    virtual bool IsApplicationInstalled(char * pchAppKey) { return g_apps.IsApplicationInstalled(pchAppKey); }
    virtual uint32_t GetApplicationCount() { return g_apps.GetApplicationCount(); }
    virtual EVRApplicationError GetApplicationKeyByIndex(uint32_t unApplicationIndex, char * pchAppKeyBuffer, uint32_t unAppKeyBufferLen) { return g_apps.GetApplicationKeyByIndex(unApplicationIndex, pchAppKeyBuffer, unAppKeyBufferLen); }
    virtual EVRApplicationError GetApplicationKeyByProcessId(uint32_t unProcessId, char * pchAppKeyBuffer, uint32_t unAppKeyBufferLen) { return g_apps.GetApplicationKeyByProcessId(unProcessId, pchAppKeyBuffer, unAppKeyBufferLen); }
    virtual EVRApplicationError LaunchApplication(char * pchAppKey) { return g_apps.LaunchApplication(pchAppKey); }
    virtual EVRApplicationError LaunchTemplateApplication(char * pchTemplateAppKey, char * pchNewAppKey, struct AppOverrideKeys_t * pKeys, uint32_t unKeys) { return g_apps.LaunchTemplateApplication(pchTemplateAppKey, pchNewAppKey, pKeys, unKeys); }
    virtual EVRApplicationError LaunchApplicationFromMimeType(char * pchMimeType, char * pchArgs) { return g_apps.LaunchApplicationFromMimeType(pchMimeType, pchArgs); }
    virtual EVRApplicationError LaunchDashboardOverlay(char * pchAppKey) { return g_apps.LaunchDashboardOverlay(pchAppKey); }
    virtual bool CancelApplicationLaunch(char * pchAppKey) { return g_apps.CancelApplicationLaunch(pchAppKey); }
    virtual EVRApplicationError IdentifyApplication(uint32_t unProcessId, char * pchAppKey) { return g_apps.IdentifyApplication(unProcessId, pchAppKey); }
    virtual uint32_t GetApplicationProcessId(char * pchAppKey) { return g_apps.GetApplicationProcessId(pchAppKey); }
    virtual char * GetApplicationsErrorNameFromEnum(EVRApplicationError error) { return g_apps.GetApplicationsErrorNameFromEnum(error); }
    virtual uint32_t GetApplicationPropertyString(char * pchAppKey, EVRApplicationProperty eProperty, char * pchPropertyValueBuffer, uint32_t unPropertyValueBufferLen, EVRApplicationError * peError) { return g_apps.GetApplicationPropertyString(pchAppKey, eProperty, pchPropertyValueBuffer, unPropertyValueBufferLen, peError); }
    virtual bool GetApplicationPropertyBool(char * pchAppKey, EVRApplicationProperty eProperty, EVRApplicationError * peError) { return g_apps.GetApplicationPropertyBool(pchAppKey, eProperty, peError); }
    virtual uint64_t GetApplicationPropertyUint64(char * pchAppKey, EVRApplicationProperty eProperty, EVRApplicationError * peError) { return g_apps.GetApplicationPropertyUint64(pchAppKey, eProperty, peError); }
    virtual EVRApplicationError SetApplicationAutoLaunch(char * pchAppKey, bool bAutoLaunch) { return g_apps.SetApplicationAutoLaunch(pchAppKey, bAutoLaunch); }
    virtual bool GetApplicationAutoLaunch(char * pchAppKey) { return g_apps.GetApplicationAutoLaunch(pchAppKey); }
    virtual EVRApplicationError SetDefaultApplicationForMimeType(char * pchAppKey, char * pchMimeType) { return g_apps.SetDefaultApplicationForMimeType(pchAppKey, pchMimeType); }
    virtual bool GetDefaultApplicationForMimeType(char * pchMimeType, char * pchAppKeyBuffer, uint32_t unAppKeyBufferLen) { return g_apps.GetDefaultApplicationForMimeType(pchMimeType, pchAppKeyBuffer, unAppKeyBufferLen); }
    virtual bool GetApplicationSupportedMimeTypes(char * pchAppKey, char * pchMimeTypesBuffer, uint32_t unMimeTypesBuffer) { return g_apps.GetApplicationSupportedMimeTypes(pchAppKey, pchMimeTypesBuffer, unMimeTypesBuffer); }
    virtual uint32_t GetApplicationsThatSupportMimeType(char * pchMimeType, char * pchAppKeysThatSupportBuffer, uint32_t unAppKeysThatSupportBuffer) { return g_apps.GetApplicationsThatSupportMimeType(pchMimeType, pchAppKeysThatSupportBuffer, unAppKeysThatSupportBuffer); }
    virtual uint32_t GetApplicationLaunchArguments(uint32_t unHandle, char * pchArgs, uint32_t unArgs) { return g_apps.GetApplicationLaunchArguments(unHandle, pchArgs, unArgs); }
    virtual EVRApplicationError GetStartingApplication(char * pchAppKeyBuffer, uint32_t unAppKeyBufferLen) { return g_apps.GetStartingApplication(pchAppKeyBuffer, unAppKeyBufferLen); }
    virtual EVRApplicationTransitionState GetTransitionState() { return g_apps.GetTransitionState(); }
    virtual EVRApplicationError PerformApplicationPrelaunchCheck(char * pchAppKey) { return g_apps.PerformApplicationPrelaunchCheck(pchAppKey); }
    virtual char * GetApplicationsTransitionStateNameFromEnum(EVRApplicationTransitionState state) { return g_apps.GetApplicationsTransitionStateNameFromEnum(state); }
    virtual bool IsQuitUserPromptRequested() { return g_apps.IsQuitUserPromptRequested(); }
    virtual EVRApplicationError LaunchInternalProcess(char * pchBinaryPath, char * pchArguments, char * pchWorkingDirectory) { return g_apps.LaunchInternalProcess(pchBinaryPath, pchArguments, pchWorkingDirectory); }
};
static ImplIVRApplications006 g_impl_g_apps;

struct ImplIVRRenderModels005 {
    virtual EVRRenderModelError LoadRenderModel_Async(char * pchRenderModelName, struct RenderModel_t ** ppRenderModel) { return g_models.LoadRenderModel_Async(pchRenderModelName, ppRenderModel); }
    virtual void FreeRenderModel(struct RenderModel_t * pRenderModel) { g_models.FreeRenderModel(pRenderModel); }
    virtual EVRRenderModelError LoadTexture_Async(TextureID_t textureId, struct RenderModel_TextureMap_t ** ppTexture) { return g_models.LoadTexture_Async(textureId, ppTexture); }
    virtual void FreeTexture(struct RenderModel_TextureMap_t * pTexture) { g_models.FreeTexture(pTexture); }
    virtual EVRRenderModelError LoadTextureD3D11_Async(TextureID_t textureId, void * pD3D11Device, void ** ppD3D11Texture2D) { return g_models.LoadTextureD3D11_Async(textureId, pD3D11Device, ppD3D11Texture2D); }
    virtual EVRRenderModelError LoadIntoTextureD3D11_Async(TextureID_t textureId, void * pDstTexture) { return g_models.LoadIntoTextureD3D11_Async(textureId, pDstTexture); }
    virtual void FreeTextureD3D11(void * pD3D11Texture2D) { g_models.FreeTextureD3D11(pD3D11Texture2D); }
    virtual uint32_t GetRenderModelName(uint32_t unRenderModelIndex, char * pchRenderModelName, uint32_t unRenderModelNameLen) { return g_models.GetRenderModelName(unRenderModelIndex, pchRenderModelName, unRenderModelNameLen); }
    virtual uint32_t GetRenderModelCount() { return g_models.GetRenderModelCount(); }
    virtual uint32_t GetComponentCount(char * pchRenderModelName) { return g_models.GetComponentCount(pchRenderModelName); }
    virtual uint32_t GetComponentName(char * pchRenderModelName, uint32_t unComponentIndex, char * pchComponentName, uint32_t unComponentNameLen) { return g_models.GetComponentName(pchRenderModelName, unComponentIndex, pchComponentName, unComponentNameLen); }
    virtual uint64_t GetComponentButtonMask(char * pchRenderModelName, char * pchComponentName) { return g_models.GetComponentButtonMask(pchRenderModelName, pchComponentName); }
    virtual uint32_t GetComponentRenderModelName(char * pchRenderModelName, char * pchComponentName, char * pchComponentRenderModelName, uint32_t unComponentRenderModelNameLen) { return g_models.GetComponentRenderModelName(pchRenderModelName, pchComponentName, pchComponentRenderModelName, unComponentRenderModelNameLen); }
    virtual bool GetComponentState(char * pchRenderModelName, char * pchComponentName, VRControllerState_t * pControllerState, struct RenderModel_ControllerMode_State_t * pState, struct RenderModel_ComponentState_t * pComponentState) { return g_models.GetComponentState(pchRenderModelName, pchComponentName, pControllerState, pState, pComponentState); }
    virtual bool RenderModelHasComponent(char * pchRenderModelName, char * pchComponentName) { return g_models.RenderModelHasComponent(pchRenderModelName, pchComponentName); }
    virtual uint32_t GetRenderModelThumbnailURL(char * pchRenderModelName, char * pchThumbnailURL, uint32_t unThumbnailURLLen, EVRRenderModelError * peError) { return g_models.GetRenderModelThumbnailURL(pchRenderModelName, pchThumbnailURL, unThumbnailURLLen, peError); }
    virtual uint32_t GetRenderModelOriginalPath(char * pchRenderModelName, char * pchOriginalPath, uint32_t unOriginalPathLen, EVRRenderModelError * peError) { return g_models.GetRenderModelOriginalPath(pchRenderModelName, pchOriginalPath, unOriginalPathLen, peError); }
    virtual char * GetRenderModelErrorNameFromEnum(EVRRenderModelError error) { return g_models.GetRenderModelErrorNameFromEnum(error); }
};
static ImplIVRRenderModels005 g_impl_g_models;

struct ImplIVRSettings001 {
    virtual char * GetSettingsErrorNameFromEnum(EVRSettingsError eError) { return g_settings.GetSettingsErrorNameFromEnum(eError); }
    virtual bool Sync(bool bForce, EVRSettingsError * peError) { return g_settings.Sync(bForce, peError); }
    virtual bool GetBool(char * pchSection, char * pchSettingsKey, bool bDefaultValue, EVRSettingsError * peError) { return g_settings.GetBool(pchSection, pchSettingsKey, bDefaultValue, peError); }
    virtual void SetBool(char * pchSection, char * pchSettingsKey, bool bValue, EVRSettingsError * peError) { g_settings.SetBool(pchSection, pchSettingsKey, bValue, peError); }
    virtual int32_t GetInt32(char * pchSection, char * pchSettingsKey, int32_t nDefaultValue, EVRSettingsError * peError) { return g_settings.GetInt32(pchSection, pchSettingsKey, nDefaultValue, peError); }
    virtual void SetInt32(char * pchSection, char * pchSettingsKey, int32_t nValue, EVRSettingsError * peError) { g_settings.SetInt32(pchSection, pchSettingsKey, nValue, peError); }
    virtual float GetFloat(char * pchSection, char * pchSettingsKey, float flDefaultValue, EVRSettingsError * peError) { return g_settings.GetFloat(pchSection, pchSettingsKey, flDefaultValue, peError); }
    virtual void SetFloat(char * pchSection, char * pchSettingsKey, float flValue, EVRSettingsError * peError) { g_settings.SetFloat(pchSection, pchSettingsKey, flValue, peError); }
    virtual void GetString(char * pchSection, char * pchSettingsKey, char * pchValue, uint32_t unValueLen, char * pchDefaultValue, EVRSettingsError * peError) { g_settings.GetString(pchSection, pchSettingsKey, pchValue, unValueLen, pchDefaultValue, peError); }
    virtual void SetString(char * pchSection, char * pchSettingsKey, char * pchValue, EVRSettingsError * peError) { g_settings.SetString(pchSection, pchSettingsKey, pchValue, peError); }
    virtual void RemoveSection(char * pchSection, EVRSettingsError * peError) { g_settings.RemoveSection(pchSection, peError); }
    virtual void RemoveKeyInSection(char * pchSection, char * pchSettingsKey, EVRSettingsError * peError) { g_settings.RemoveKeyInSection(pchSection, pchSettingsKey, peError); }
};
static ImplIVRSettings001 g_impl_g_settings;

struct ImplIVRChaperone003 {
    virtual ChaperoneCalibrationState GetCalibrationState() { return g_chap.GetCalibrationState(); }
    virtual bool GetPlayAreaSize(float * pSizeX, float * pSizeZ) { return g_chap.GetPlayAreaSize(pSizeX, pSizeZ); }
    virtual bool GetPlayAreaRect(struct HmdQuad_t * rect) { return g_chap.GetPlayAreaRect(rect); }
    virtual void ReloadInfo() { g_chap.ReloadInfo(); }
    virtual void SetSceneColor(struct HmdColor_t color) { g_chap.SetSceneColor(color); }
    virtual void GetBoundsColor(struct HmdColor_t * pOutputColorArray, int nNumOutputColors, float flCollisionBoundsFadeDistance, struct HmdColor_t * pOutputCameraColor) { g_chap.GetBoundsColor(pOutputColorArray, nNumOutputColors, flCollisionBoundsFadeDistance, pOutputCameraColor); }
    virtual bool AreBoundsVisible() { return g_chap.AreBoundsVisible(); }
    virtual void ForceBoundsVisible(bool bForce) { g_chap.ForceBoundsVisible(bForce); }
};
static ImplIVRChaperone003 g_impl_g_chap;

struct ImplIVRChaperoneSetup005 {
    virtual bool CommitWorkingCopy(EChaperoneConfigFile configFile) { return g_chapSetup.CommitWorkingCopy(configFile); }
    virtual void RevertWorkingCopy() { g_chapSetup.RevertWorkingCopy(); }
    virtual bool GetWorkingPlayAreaSize(float * pSizeX, float * pSizeZ) { return g_chapSetup.GetWorkingPlayAreaSize(pSizeX, pSizeZ); }
    virtual bool GetWorkingPlayAreaRect(struct HmdQuad_t * rect) { return g_chapSetup.GetWorkingPlayAreaRect(rect); }
    virtual bool GetWorkingCollisionBoundsInfo(struct HmdQuad_t * pQuadsBuffer, uint32_t * punQuadsCount) { return g_chapSetup.GetWorkingCollisionBoundsInfo(pQuadsBuffer, punQuadsCount); }
    virtual bool GetLiveCollisionBoundsInfo(struct HmdQuad_t * pQuadsBuffer, uint32_t * punQuadsCount) { return g_chapSetup.GetLiveCollisionBoundsInfo(pQuadsBuffer, punQuadsCount); }
    virtual bool GetWorkingSeatedZeroPoseToRawTrackingPose(struct HmdMatrix34_t * pmatSeatedZeroPoseToRawTrackingPose) { return g_chapSetup.GetWorkingSeatedZeroPoseToRawTrackingPose(pmatSeatedZeroPoseToRawTrackingPose); }
    virtual bool GetWorkingStandingZeroPoseToRawTrackingPose(struct HmdMatrix34_t * pmatStandingZeroPoseToRawTrackingPose) { return g_chapSetup.GetWorkingStandingZeroPoseToRawTrackingPose(pmatStandingZeroPoseToRawTrackingPose); }
    virtual void SetWorkingPlayAreaSize(float sizeX, float sizeZ) { g_chapSetup.SetWorkingPlayAreaSize(sizeX, sizeZ); }
    virtual void SetWorkingCollisionBoundsInfo(struct HmdQuad_t * pQuadsBuffer, uint32_t unQuadsCount) { g_chapSetup.SetWorkingCollisionBoundsInfo(pQuadsBuffer, unQuadsCount); }
    virtual void SetWorkingSeatedZeroPoseToRawTrackingPose(struct HmdMatrix34_t * pMatSeatedZeroPoseToRawTrackingPose) { g_chapSetup.SetWorkingSeatedZeroPoseToRawTrackingPose(pMatSeatedZeroPoseToRawTrackingPose); }
    virtual void SetWorkingStandingZeroPoseToRawTrackingPose(struct HmdMatrix34_t * pMatStandingZeroPoseToRawTrackingPose) { g_chapSetup.SetWorkingStandingZeroPoseToRawTrackingPose(pMatStandingZeroPoseToRawTrackingPose); }
    virtual void ReloadFromDisk(EChaperoneConfigFile configFile) { g_chapSetup.ReloadFromDisk(configFile); }
    virtual bool GetLiveSeatedZeroPoseToRawTrackingPose(struct HmdMatrix34_t * pmatSeatedZeroPoseToRawTrackingPose) { return g_chapSetup.GetLiveSeatedZeroPoseToRawTrackingPose(pmatSeatedZeroPoseToRawTrackingPose); }
    virtual void SetWorkingCollisionBoundsTagsInfo(uint8_t * pTagsBuffer, uint32_t unTagCount) { g_chapSetup.SetWorkingCollisionBoundsTagsInfo(pTagsBuffer, unTagCount); }
    virtual bool GetLiveCollisionBoundsTagsInfo(uint8_t * pTagsBuffer, uint32_t * punTagCount) { return g_chapSetup.GetLiveCollisionBoundsTagsInfo(pTagsBuffer, punTagCount); }
    virtual bool SetWorkingPhysicalBoundsInfo(struct HmdQuad_t * pQuadsBuffer, uint32_t unQuadsCount) { return g_chapSetup.SetWorkingPhysicalBoundsInfo(pQuadsBuffer, unQuadsCount); }
    virtual bool GetLivePhysicalBoundsInfo(struct HmdQuad_t * pQuadsBuffer, uint32_t * punQuadsCount) { return g_chapSetup.GetLivePhysicalBoundsInfo(pQuadsBuffer, punQuadsCount); }
    virtual bool ExportLiveToBuffer(char * pBuffer, uint32_t * pnBufferLength) { return g_chapSetup.ExportLiveToBuffer(pBuffer, pnBufferLength); }
    virtual bool ImportFromBufferToWorking(char * pBuffer, uint32_t nImportFlags) { return g_chapSetup.ImportFromBufferToWorking(pBuffer, nImportFlags); }
};
static ImplIVRChaperoneSetup005 g_impl_g_chapSetup;

struct ImplIVROverlay013 {
    virtual EVROverlayError FindOverlay(char * pchOverlayKey, VROverlayHandle_t * pOverlayHandle) { return g_overlay.FindOverlay(pchOverlayKey, pOverlayHandle); }
    virtual EVROverlayError CreateOverlay(char * pchOverlayKey, char * pchOverlayFriendlyName, VROverlayHandle_t * pOverlayHandle) { return g_overlay.CreateOverlay(pchOverlayKey, pchOverlayFriendlyName, pOverlayHandle); }
    virtual EVROverlayError DestroyOverlay(VROverlayHandle_t ulOverlayHandle) { return g_overlay.DestroyOverlay(ulOverlayHandle); }
    virtual EVROverlayError SetHighQualityOverlay(VROverlayHandle_t ulOverlayHandle) { return g_overlay.SetHighQualityOverlay(ulOverlayHandle); }
    virtual VROverlayHandle_t GetHighQualityOverlay() { return g_overlay.GetHighQualityOverlay(); }
    virtual uint32_t GetOverlayKey(VROverlayHandle_t ulOverlayHandle, char * pchValue, uint32_t unBufferSize, EVROverlayError * pError) { return g_overlay.GetOverlayKey(ulOverlayHandle, pchValue, unBufferSize, pError); }
    virtual uint32_t GetOverlayName(VROverlayHandle_t ulOverlayHandle, char * pchValue, uint32_t unBufferSize, EVROverlayError * pError) { return g_overlay.GetOverlayName(ulOverlayHandle, pchValue, unBufferSize, pError); }
    virtual EVROverlayError GetOverlayImageData(VROverlayHandle_t ulOverlayHandle, void * pvBuffer, uint32_t unBufferSize, uint32_t * punWidth, uint32_t * punHeight) { return g_overlay.GetOverlayImageData(ulOverlayHandle, pvBuffer, unBufferSize, punWidth, punHeight); }
    virtual char * GetOverlayErrorNameFromEnum(EVROverlayError error) { return g_overlay.GetOverlayErrorNameFromEnum(error); }
    virtual EVROverlayError SetOverlayRenderingPid(VROverlayHandle_t ulOverlayHandle, uint32_t unPID) { return g_overlay.SetOverlayRenderingPid(ulOverlayHandle, unPID); }
    virtual uint32_t GetOverlayRenderingPid(VROverlayHandle_t ulOverlayHandle) { return g_overlay.GetOverlayRenderingPid(ulOverlayHandle); }
    virtual EVROverlayError SetOverlayFlag(VROverlayHandle_t ulOverlayHandle, VROverlayFlags eOverlayFlag, bool bEnabled) { return g_overlay.SetOverlayFlag(ulOverlayHandle, eOverlayFlag, bEnabled); }
    virtual EVROverlayError GetOverlayFlag(VROverlayHandle_t ulOverlayHandle, VROverlayFlags eOverlayFlag, bool * pbEnabled) { return g_overlay.GetOverlayFlag(ulOverlayHandle, eOverlayFlag, pbEnabled); }
    virtual EVROverlayError SetOverlayColor(VROverlayHandle_t ulOverlayHandle, float fRed, float fGreen, float fBlue) { return g_overlay.SetOverlayColor(ulOverlayHandle, fRed, fGreen, fBlue); }
    virtual EVROverlayError GetOverlayColor(VROverlayHandle_t ulOverlayHandle, float * pfRed, float * pfGreen, float * pfBlue) { return g_overlay.GetOverlayColor(ulOverlayHandle, pfRed, pfGreen, pfBlue); }
    virtual EVROverlayError SetOverlayAlpha(VROverlayHandle_t ulOverlayHandle, float fAlpha) { return g_overlay.SetOverlayAlpha(ulOverlayHandle, fAlpha); }
    virtual EVROverlayError GetOverlayAlpha(VROverlayHandle_t ulOverlayHandle, float * pfAlpha) { return g_overlay.GetOverlayAlpha(ulOverlayHandle, pfAlpha); }
    virtual EVROverlayError SetOverlayTexelAspect(VROverlayHandle_t ulOverlayHandle, float fTexelAspect) { return g_overlay.SetOverlayTexelAspect(ulOverlayHandle, fTexelAspect); }
    virtual EVROverlayError GetOverlayTexelAspect(VROverlayHandle_t ulOverlayHandle, float * pfTexelAspect) { return g_overlay.GetOverlayTexelAspect(ulOverlayHandle, pfTexelAspect); }
    virtual EVROverlayError SetOverlaySortOrder(VROverlayHandle_t ulOverlayHandle, uint32_t unSortOrder) { return g_overlay.SetOverlaySortOrder(ulOverlayHandle, unSortOrder); }
    virtual EVROverlayError GetOverlaySortOrder(VROverlayHandle_t ulOverlayHandle, uint32_t * punSortOrder) { return g_overlay.GetOverlaySortOrder(ulOverlayHandle, punSortOrder); }
    virtual EVROverlayError SetOverlayWidthInMeters(VROverlayHandle_t ulOverlayHandle, float fWidthInMeters) { return g_overlay.SetOverlayWidthInMeters(ulOverlayHandle, fWidthInMeters); }
    virtual EVROverlayError GetOverlayWidthInMeters(VROverlayHandle_t ulOverlayHandle, float * pfWidthInMeters) { return g_overlay.GetOverlayWidthInMeters(ulOverlayHandle, pfWidthInMeters); }
    virtual EVROverlayError SetOverlayAutoCurveDistanceRangeInMeters(VROverlayHandle_t ulOverlayHandle, float fMinDistanceInMeters, float fMaxDistanceInMeters) { return g_overlay.SetOverlayAutoCurveDistanceRangeInMeters(ulOverlayHandle, fMinDistanceInMeters, fMaxDistanceInMeters); }
    virtual EVROverlayError GetOverlayAutoCurveDistanceRangeInMeters(VROverlayHandle_t ulOverlayHandle, float * pfMinDistanceInMeters, float * pfMaxDistanceInMeters) { return g_overlay.GetOverlayAutoCurveDistanceRangeInMeters(ulOverlayHandle, pfMinDistanceInMeters, pfMaxDistanceInMeters); }
    virtual EVROverlayError SetOverlayTextureColorSpace(VROverlayHandle_t ulOverlayHandle, EColorSpace eTextureColorSpace) { return g_overlay.SetOverlayTextureColorSpace(ulOverlayHandle, eTextureColorSpace); }
    virtual EVROverlayError GetOverlayTextureColorSpace(VROverlayHandle_t ulOverlayHandle, EColorSpace * peTextureColorSpace) { return g_overlay.GetOverlayTextureColorSpace(ulOverlayHandle, peTextureColorSpace); }
    virtual EVROverlayError SetOverlayTextureBounds(VROverlayHandle_t ulOverlayHandle, struct VRTextureBounds_t * pOverlayTextureBounds) { return g_overlay.SetOverlayTextureBounds(ulOverlayHandle, pOverlayTextureBounds); }
    virtual EVROverlayError GetOverlayTextureBounds(VROverlayHandle_t ulOverlayHandle, struct VRTextureBounds_t * pOverlayTextureBounds) { return g_overlay.GetOverlayTextureBounds(ulOverlayHandle, pOverlayTextureBounds); }
    virtual EVROverlayError GetOverlayTransformType(VROverlayHandle_t ulOverlayHandle, VROverlayTransformType * peTransformType) { return g_overlay.GetOverlayTransformType(ulOverlayHandle, peTransformType); }
    virtual EVROverlayError SetOverlayTransformAbsolute(VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, struct HmdMatrix34_t * pmatTrackingOriginToOverlayTransform) { return g_overlay.SetOverlayTransformAbsolute(ulOverlayHandle, eTrackingOrigin, pmatTrackingOriginToOverlayTransform); }
    virtual EVROverlayError GetOverlayTransformAbsolute(VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin * peTrackingOrigin, struct HmdMatrix34_t * pmatTrackingOriginToOverlayTransform) { return g_overlay.GetOverlayTransformAbsolute(ulOverlayHandle, peTrackingOrigin, pmatTrackingOriginToOverlayTransform); }
    virtual EVROverlayError SetOverlayTransformTrackedDeviceRelative(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unTrackedDevice, struct HmdMatrix34_t * pmatTrackedDeviceToOverlayTransform) { return g_overlay.SetOverlayTransformTrackedDeviceRelative(ulOverlayHandle, unTrackedDevice, pmatTrackedDeviceToOverlayTransform); }
    virtual EVROverlayError GetOverlayTransformTrackedDeviceRelative(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t * punTrackedDevice, struct HmdMatrix34_t * pmatTrackedDeviceToOverlayTransform) { return g_overlay.GetOverlayTransformTrackedDeviceRelative(ulOverlayHandle, punTrackedDevice, pmatTrackedDeviceToOverlayTransform); }
    virtual EVROverlayError SetOverlayTransformTrackedDeviceComponent(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unDeviceIndex, char * pchComponentName) { return g_overlay.SetOverlayTransformTrackedDeviceComponent(ulOverlayHandle, unDeviceIndex, pchComponentName); }
    virtual EVROverlayError GetOverlayTransformTrackedDeviceComponent(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t * punDeviceIndex, char * pchComponentName, uint32_t unComponentNameSize) { return g_overlay.GetOverlayTransformTrackedDeviceComponent(ulOverlayHandle, punDeviceIndex, pchComponentName, unComponentNameSize); }
    virtual EVROverlayError ShowOverlay(VROverlayHandle_t ulOverlayHandle) { return g_overlay.ShowOverlay(ulOverlayHandle); }
    virtual EVROverlayError HideOverlay(VROverlayHandle_t ulOverlayHandle) { return g_overlay.HideOverlay(ulOverlayHandle); }
    virtual bool IsOverlayVisible(VROverlayHandle_t ulOverlayHandle) { return g_overlay.IsOverlayVisible(ulOverlayHandle); }
    virtual EVROverlayError GetTransformForOverlayCoordinates(VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, struct HmdVector2_t coordinatesInOverlay, struct HmdMatrix34_t * pmatTransform) { return g_overlay.GetTransformForOverlayCoordinates(ulOverlayHandle, eTrackingOrigin, coordinatesInOverlay, pmatTransform); }
    virtual bool PollNextOverlayEvent(VROverlayHandle_t ulOverlayHandle, struct VREvent_t * pEvent, uint32_t uncbVREvent) { return g_overlay.PollNextOverlayEvent(ulOverlayHandle, pEvent, uncbVREvent); }
    virtual EVROverlayError GetOverlayInputMethod(VROverlayHandle_t ulOverlayHandle, VROverlayInputMethod * peInputMethod) { return g_overlay.GetOverlayInputMethod(ulOverlayHandle, peInputMethod); }
    virtual EVROverlayError SetOverlayInputMethod(VROverlayHandle_t ulOverlayHandle, VROverlayInputMethod eInputMethod) { return g_overlay.SetOverlayInputMethod(ulOverlayHandle, eInputMethod); }
    virtual EVROverlayError GetOverlayMouseScale(VROverlayHandle_t ulOverlayHandle, struct HmdVector2_t * pvecMouseScale) { return g_overlay.GetOverlayMouseScale(ulOverlayHandle, pvecMouseScale); }
    virtual EVROverlayError SetOverlayMouseScale(VROverlayHandle_t ulOverlayHandle, struct HmdVector2_t * pvecMouseScale) { return g_overlay.SetOverlayMouseScale(ulOverlayHandle, pvecMouseScale); }
    virtual bool ComputeOverlayIntersection(VROverlayHandle_t ulOverlayHandle, struct VROverlayIntersectionParams_t * pParams, struct VROverlayIntersectionResults_t * pResults) { return g_overlay.ComputeOverlayIntersection(ulOverlayHandle, pParams, pResults); }
    virtual bool HandleControllerOverlayInteractionAsMouse(VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unControllerDeviceIndex) { return g_overlay.HandleControllerOverlayInteractionAsMouse(ulOverlayHandle, unControllerDeviceIndex); }
    virtual bool IsHoverTargetOverlay(VROverlayHandle_t ulOverlayHandle) { return g_overlay.IsHoverTargetOverlay(ulOverlayHandle); }
    virtual VROverlayHandle_t GetGamepadFocusOverlay() { return g_overlay.GetGamepadFocusOverlay(); }
    virtual EVROverlayError SetGamepadFocusOverlay(VROverlayHandle_t ulNewFocusOverlay) { return g_overlay.SetGamepadFocusOverlay(ulNewFocusOverlay); }
    virtual EVROverlayError SetOverlayNeighbor(EOverlayDirection eDirection, VROverlayHandle_t ulFrom, VROverlayHandle_t ulTo) { return g_overlay.SetOverlayNeighbor(eDirection, ulFrom, ulTo); }
    virtual EVROverlayError MoveGamepadFocusToNeighbor(EOverlayDirection eDirection, VROverlayHandle_t ulFrom) { return g_overlay.MoveGamepadFocusToNeighbor(eDirection, ulFrom); }
    virtual EVROverlayError SetOverlayTexture(VROverlayHandle_t ulOverlayHandle, struct Texture_t * pTexture) { return g_overlay.SetOverlayTexture(ulOverlayHandle, pTexture); }
    virtual EVROverlayError ClearOverlayTexture(VROverlayHandle_t ulOverlayHandle) { return g_overlay.ClearOverlayTexture(ulOverlayHandle); }
    virtual EVROverlayError SetOverlayRaw(VROverlayHandle_t ulOverlayHandle, void * pvBuffer, uint32_t unWidth, uint32_t unHeight, uint32_t unDepth) { return g_overlay.SetOverlayRaw(ulOverlayHandle, pvBuffer, unWidth, unHeight, unDepth); }
    virtual EVROverlayError SetOverlayFromFile(VROverlayHandle_t ulOverlayHandle, char * pchFilePath) { return g_overlay.SetOverlayFromFile(ulOverlayHandle, pchFilePath); }
    virtual EVROverlayError GetOverlayTexture(VROverlayHandle_t ulOverlayHandle, void ** pNativeTextureHandle, void * pNativeTextureRef, uint32_t * pWidth, uint32_t * pHeight, uint32_t * pNativeFormat, EGraphicsAPIConvention * pAPI, EColorSpace * pColorSpace) { return g_overlay.GetOverlayTexture(ulOverlayHandle, pNativeTextureHandle, pNativeTextureRef, pWidth, pHeight, pNativeFormat, pAPI, pColorSpace); }
    virtual EVROverlayError ReleaseNativeOverlayHandle(VROverlayHandle_t ulOverlayHandle, void * pNativeTextureHandle) { return g_overlay.ReleaseNativeOverlayHandle(ulOverlayHandle, pNativeTextureHandle); }
    virtual EVROverlayError GetOverlayTextureSize(VROverlayHandle_t ulOverlayHandle, uint32_t * pWidth, uint32_t * pHeight) { return g_overlay.GetOverlayTextureSize(ulOverlayHandle, pWidth, pHeight); }
    virtual EVROverlayError CreateDashboardOverlay(char * pchOverlayKey, char * pchOverlayFriendlyName, VROverlayHandle_t * pMainHandle, VROverlayHandle_t * pThumbnailHandle) { return g_overlay.CreateDashboardOverlay(pchOverlayKey, pchOverlayFriendlyName, pMainHandle, pThumbnailHandle); }
    virtual bool IsDashboardVisible() { return g_overlay.IsDashboardVisible(); }
    virtual bool IsActiveDashboardOverlay(VROverlayHandle_t ulOverlayHandle) { return g_overlay.IsActiveDashboardOverlay(ulOverlayHandle); }
    virtual EVROverlayError SetDashboardOverlaySceneProcess(VROverlayHandle_t ulOverlayHandle, uint32_t unProcessId) { return g_overlay.SetDashboardOverlaySceneProcess(ulOverlayHandle, unProcessId); }
    virtual EVROverlayError GetDashboardOverlaySceneProcess(VROverlayHandle_t ulOverlayHandle, uint32_t * punProcessId) { return g_overlay.GetDashboardOverlaySceneProcess(ulOverlayHandle, punProcessId); }
    virtual void ShowDashboard(char * pchOverlayToShow) { g_overlay.ShowDashboard(pchOverlayToShow); }
    virtual TrackedDeviceIndex_t GetPrimaryDashboardDevice() { return g_overlay.GetPrimaryDashboardDevice(); }
    virtual EVROverlayError ShowKeyboard(EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, char * pchDescription, uint32_t unCharMax, char * pchExistingText, bool bUseMinimalMode, uint64_t uUserValue) { return g_overlay.ShowKeyboard(eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, bUseMinimalMode, uUserValue); }
    virtual EVROverlayError ShowKeyboardForOverlay(VROverlayHandle_t ulOverlayHandle, EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, char * pchDescription, uint32_t unCharMax, char * pchExistingText, bool bUseMinimalMode, uint64_t uUserValue) { return g_overlay.ShowKeyboardForOverlay(ulOverlayHandle, eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, bUseMinimalMode, uUserValue); }
    virtual uint32_t GetKeyboardText(char * pchText, uint32_t cchText) { return g_overlay.GetKeyboardText(pchText, cchText); }
    virtual void HideKeyboard() { g_overlay.HideKeyboard(); }
    virtual void SetKeyboardTransformAbsolute(ETrackingUniverseOrigin eTrackingOrigin, struct HmdMatrix34_t * pmatTrackingOriginToKeyboardTransform) { g_overlay.SetKeyboardTransformAbsolute(eTrackingOrigin, pmatTrackingOriginToKeyboardTransform); }
    virtual void SetKeyboardPositionForOverlay(VROverlayHandle_t ulOverlayHandle, struct HmdRect2_t avoidRect) { g_overlay.SetKeyboardPositionForOverlay(ulOverlayHandle, avoidRect); }
};
static ImplIVROverlay013 g_impl_g_overlay;
