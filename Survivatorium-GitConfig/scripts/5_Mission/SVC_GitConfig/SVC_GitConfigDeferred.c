#ifdef SERVER

// Full GitConfig sync — runs at 5_Mission init where RestApi is available.
// In DayZ 1.29, CreateRestApi() returns null during 3_Game #InitGlobals,
// so we moved the entire sync here. $profile: is still writable here,
// and this runs before AfterHiveInit() so CE config files are ready in time.
int svc_gitconfig_deferred_init = SVC_GitConfigDeferredInit();

int SVC_GitConfigDeferredInit()
{
	SVC_GitConfigMain.Run();
	SVC_GitConfigMain.ProcessDeferredFiles();
	return 1;
}

#endif
