class SVC_GitConfigSettings
{
	int version = 1;
	string githubToken = "";
	string repoOwner = "";
	string repoName = "";
	string branch = "main";
	string serverFolder = "default";
	string proxyUrl = "http://127.0.0.1:8470";
	bool enableMissionSync = false;
	bool enableProfileSync = true;
	bool enableSavesSync = false;
	int maxRetries = 3;
	int maxFileSizeMB = 50;
	bool abortOnTreeFetchFail = true;
	bool blockScriptFiles = true;
	bool permitAllExtensions = false;
	int proxyWriteThresholdKB = 10240;
	bool forceRedownload = false;
	ref array<string> binaryExtensions = new array<string>();
	ref array<string> allowedExtensions = new array<string>();
	ref map<string, string> lastFileHashes = new map<string, string>();

	void InitDefaults()
	{
		if (allowedExtensions.Count() == 0)
		{
			allowedExtensions.Insert(".json");
			allowedExtensions.Insert(".xml");
			allowedExtensions.Insert(".cfg");
			allowedExtensions.Insert(".txt");
			allowedExtensions.Insert(".csv");
			allowedExtensions.Insert(".map");
		}
		if (binaryExtensions.Count() == 0)
		{
			binaryExtensions.Insert(".dze");
			binaryExtensions.Insert(".map");
			binaryExtensions.Insert(".bin");
			binaryExtensions.Insert(".pbo");
			binaryExtensions.Insert(".pak");
		}
	}
}
