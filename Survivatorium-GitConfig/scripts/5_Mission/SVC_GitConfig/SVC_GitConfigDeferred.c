#ifdef SERVER

// Deferred mission file sync — runs at 5_Mission init, after $mission: is available
int svc_gitconfig_deferred_init = SVC_GitConfigDeferredInit();

int SVC_GitConfigDeferredInit()
{
	SVC_GitConfigMain.ProcessDeferredFiles();
	return 1;
}

#endif
