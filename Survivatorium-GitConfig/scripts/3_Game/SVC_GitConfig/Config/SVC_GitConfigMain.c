#ifdef SERVER

// NOTE: In DayZ 1.29, CreateRestApi() returns null during #InitGlobals (3_Game static init).
// The full sync is now deferred to 5_Mission where RestApi is available.
// See SVC_GitConfigDeferred.c

class SVC_GitConfigMain
{
	static ref SVC_GitConfigSettings m_Settings;
	static ref array<ref SVC_GitTreeEntry> m_DeferredMissionFiles;
	static bool m_FileChanged;
	static int m_TotalFiles;
	static int m_SkippedFiles;
	static int m_DownloadedFiles;
	static int m_FailedFiles;

	static const string CONFIG_PATH = "$profile:Survivatorium/GitConfig/config.json";
	static const string LOG_PREFIX = "[SVC-GitConfig] ";

	// Known error response prefixes that indicate a failed download
	static const string ERR_SERVER = "Server Error";
	static const string ERR_CLIENT = "Client Error";
	static const string ERR_TIMEOUT = "Timeout";
	static const string ERR_NOT_FOUND = "404: Not Found";
	static const string ERR_HTML_OPEN = "<!DOCTYPE";
	static const string ERR_HTML_TAG = "<html";

	// =========================================================================
	// Entry point — called during 3_Game static initialization
	// =========================================================================
	static int Run()
	{
		m_DeferredMissionFiles = new array<ref SVC_GitTreeEntry>();
		m_FileChanged = false;
		m_TotalFiles = 0;
		m_SkippedFiles = 0;
		m_DownloadedFiles = 0;
		m_FailedFiles = 0;

		if (!LoadConfig())
			return 0;

		if (m_Settings.githubToken == "" || m_Settings.repoOwner == "" || m_Settings.repoName == "")
		{
			Print(LOG_PREFIX + "Configuration incomplete. Please edit: " + CONFIG_PATH);
			return 0;
		}

		if (m_Settings.proxyUrl == "")
		{
			Error(LOG_PREFIX + "proxyUrl is empty. Set it to http://127.0.0.1:8470 and start the proxy.");
			return 0;
		}

		if (!ValidateConfig())
			return 0;

		if (m_Settings.forceRedownload)
		{
			Print(LOG_PREFIX + "forceRedownload is ON — clearing all cached hashes.");
			m_Settings.lastFileHashes.Clear();
		}

		int startTime = TickCount(0);

		ref array<ref SVC_GitTreeEntry> files = FetchFileTree();
		if (!files)
			return 0;

		ProcessFiles(files);
		WaitForLfsDownloads();

		if (m_FileChanged)
			SaveConfig();

		if (m_Settings.forceRedownload)
		{
			m_Settings.forceRedownload = false;
			SaveConfig();
			Print(LOG_PREFIX + "forceRedownload reset to OFF.");
		}

		int elapsed = TickCount(startTime) / 10000;
		Print(LOG_PREFIX + "Profile/Saves sync complete in " + elapsed.ToString() + "ms | Total: " + m_TotalFiles.ToString() + " | Downloaded: " + m_DownloadedFiles.ToString() + " | Skipped: " + m_SkippedFiles.ToString() + " | Failed: " + m_FailedFiles.ToString());

		if (m_DeferredMissionFiles.Count() > 0)
		{
			Print(LOG_PREFIX + m_DeferredMissionFiles.Count().ToString() + " mission file(s) deferred to mission init.");
		}

		return 1;
	}

	// =========================================================================
	// Config loading / saving
	// =========================================================================
	static bool LoadConfig()
	{
		string error;
		if (!FileExist(CONFIG_PATH))
		{
			m_Settings = new SVC_GitConfigSettings();
			m_Settings.InitDefaults();
			if (!SaveConfig())
				return false;
			Print(LOG_PREFIX + "Default config created at " + CONFIG_PATH);
			Print(LOG_PREFIX + "Please configure your GitHub token, repo owner, and repo name, then restart the server.");
			return false;
		}

		if (!JsonFileLoader<SVC_GitConfigSettings>.LoadFile(CONFIG_PATH, m_Settings, error))
		{
			Error(LOG_PREFIX + "Failed to load config: " + error);
			return false;
		}
		return true;
	}

	static bool SaveConfig()
	{
		string error;
		CreateParentDirectories(CONFIG_PATH);
		if (!JsonFileLoader<SVC_GitConfigSettings>.SaveFile(CONFIG_PATH, m_Settings, error))
		{
			Error(LOG_PREFIX + "Failed to save config: " + error);
			return false;
		}
		return true;
	}

	// =========================================================================
	// Validation
	// =========================================================================
	static bool ValidateConfig()
	{
		bool ownerUnsafe = ContainsUnsafeChars(m_Settings.repoOwner);
		bool nameUnsafe = ContainsUnsafeChars(m_Settings.repoName);
		bool branchUnsafe = ContainsUnsafeChars(m_Settings.branch);
		bool folderUnsafe = ContainsUnsafeChars(m_Settings.serverFolder);
		if (ownerUnsafe || nameUnsafe || branchUnsafe || folderUnsafe)
		{
			Error(LOG_PREFIX + "Config contains invalid characters in repo/branch/folder fields. Allowed: alphanumeric, dash, underscore, dot.");
			return false;
		}

		if (m_Settings.enableMissionSync)
		{
			Print(LOG_PREFIX + "##############################################################");
			Print(LOG_PREFIX + "  WARNING: Mission sync is ENABLED.");
			Print(LOG_PREFIX + "  Files will be written to the $mission: directory.");
			Print(LOG_PREFIX + "  Only enable this if you fully trust your GitHub repo security.");
			Print(LOG_PREFIX + "##############################################################");
		}

		return true;
	}

	static bool ContainsUnsafeChars(string val)
	{
		if (val.Contains("..")) return true;
		// Forward slash now allowed — enables nested serverFolder paths (e.g. "servers/v5")
    	// Path traversal is still blocked by the ".." check above
		// if (val.Contains("/")) return true;
		if (val.Contains("\\")) return true;
		if (val.Contains("?")) return true;
		if (val.Contains("&")) return true;
		if (val.Contains("#")) return true;
		if (val.Contains(" ")) return true;
		if (val.Contains("@")) return true;
		if (val.Contains(":")) return true;
		return false;
	}

	// =========================================================================
	// Fetch repository file tree via local proxy
	// =========================================================================
	static ref array<ref SVC_GitTreeEntry> FetchFileTree()
	{
		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();

		string treeUrl = m_Settings.proxyUrl + "/tree?owner=" + m_Settings.repoOwner + "&repo=" + m_Settings.repoName + "&branch=" + m_Settings.branch;
		if (m_Settings.githubToken != "")
			treeUrl = treeUrl + "&token=" + m_Settings.githubToken;

		Print(LOG_PREFIX + "Fetching file tree via proxy at " + m_Settings.proxyUrl);

		int maxRetries = m_Settings.maxRetries;
		if (maxRetries < 1)
			maxRetries = 1;

		string response = "";
		bool success = false;

		for (int attempt = 1; attempt <= maxRetries; attempt++)
		{
			RestContext ctx = api.GetRestContext(treeUrl);
			ctx.SetHeader("application/json");
			response = ctx.GET_now("");

			if (response.Length() > 0 && response.IndexOf("Client Error") != 0 && response.IndexOf("Server Error") != 0)
			{
				success = true;
				break;
			}

			if (attempt < maxRetries)
				Print(LOG_PREFIX + "Tree fetch attempt " + attempt.ToString() + " failed, retrying...");

			response = "";
		}

		if (!success || response == "")
		{
			Error(LOG_PREFIX + "Failed to fetch file tree after " + maxRetries.ToString() + " attempts.");
			Print(LOG_PREFIX + "Is the proxy running? Start it with: svc-gitconfig-proxy.exe");
			Print(LOG_PREFIX + "Expected proxy at: " + m_Settings.proxyUrl);
			return null;
		}

		Print(LOG_PREFIX + "Tree response: " + response.Length().ToString() + " bytes");

		SVC_GitTreeResponse treeResponse;
		string error;
		if (!JsonFileLoader<SVC_GitTreeResponse>.LoadData(response, treeResponse, error))
		{
			Error(LOG_PREFIX + "Failed to parse GitHub tree response: " + error);
			return null;
		}

		if (!treeResponse || !treeResponse.tree)
		{
			Error(LOG_PREFIX + "Invalid tree data in GitHub response.");
			return null;
		}

		if (treeResponse.truncated)
		{
			Print(LOG_PREFIX + "WARNING: GitHub truncated the tree response. Some files may be missing. Consider reducing repo size.");
		}

		Print(LOG_PREFIX + "Received " + treeResponse.tree.Count().ToString() + " entries from repository.");
		return treeResponse.tree;
	}

	// =========================================================================
	// File processing — filter, cache-check, download
	// =========================================================================
	static void ProcessFiles(array<ref SVC_GitTreeEntry> entries)
	{
		string prefix = "";
		int prefixLen = 0;
		if (m_Settings.serverFolder != "")
		{
			prefix = m_Settings.serverFolder + "/";
			prefixLen = prefix.Length();
		}

		for (int i = 0; i < entries.Count(); i++)
		{
			SVC_GitTreeEntry entry = entries[i];

			// Only process files (blobs), skip directories (trees)
			if (entry.type != "blob")
				continue;

			// Must be under our server folder
			if (prefixLen > 0)
			{
				if (entry.path.Length() <= prefixLen)
					continue;
				if (entry.path.IndexOf(prefix) != 0)
					continue;
			}

			// Get relative path after server folder prefix
			string relativePath = entry.path;
			if (prefixLen > 0)
				relativePath = entry.path.Substring(prefixLen, entry.path.Length() - prefixLen);

			// Map to local DayZ path
			string localPath = MapToLocalPath(relativePath);
			if (localPath == "")
				continue;

			m_TotalFiles++;

			// Security: block path traversal
			if (relativePath.Contains(".."))
			{
				Print(LOG_PREFIX + "BLOCKED path traversal: " + relativePath);
				m_SkippedFiles++;
				continue;
			}

			// Security: extension whitelist
			if (!IsAllowedExtension(relativePath))
			{
				Print(LOG_PREFIX + "Skipped (extension blocked): " + relativePath);
				m_SkippedFiles++;
				continue;
			}

			// Size check — Git Trees API provides size in bytes
			if (entry.size <= 0)
			{
				// Create empty marker files directly — no download needed
				string emptyFileSha;
				if (m_Settings.lastFileHashes.Find(localPath, emptyFileSha))
				{
					if (emptyFileSha == entry.sha && FileExist(localPath))
					{
						m_SkippedFiles++;
						continue;
					}
				}
				CreateParentDirectories(localPath);
				FileHandle fh = OpenFile(localPath, FileMode.WRITE);
				if (fh != 0)
				{
					CloseFile(fh);
					m_Settings.lastFileHashes.Set(localPath, entry.sha);
					m_FileChanged = true;
					m_DownloadedFiles++;
					Print(LOG_PREFIX + "Created (empty marker): " + localPath);
				}
				continue;
			}
			int maxBytes = m_Settings.maxFileSizeMB * 1048576;
			if (maxBytes > 0 && entry.size > maxBytes)
			{
				Print(LOG_PREFIX + "Skipped (too large: " + entry.size.ToString() + " bytes, limit " + maxBytes.ToString() + "): " + relativePath);
				m_SkippedFiles++;
				continue;
			}

			// Mission files are deferred to 5_Mission init
			if (relativePath.IndexOf("mission/") == 0)
			{
				m_DeferredMissionFiles.Insert(entry);
				continue;
			}

			// SHA cache check — skip if unchanged and file exists locally
			string cachedSha;
			if (m_Settings.lastFileHashes.Find(localPath, cachedSha))
			{
				if (cachedSha == entry.sha && FileExist(localPath))
				{
					m_SkippedFiles++;
					continue;
				}
			}

			// Download and write
			DownloadAndWrite(entry.path, localPath, entry.sha, entry.size);
		}
	}

	// =========================================================================
	// Path mapping — repo structure to DayZ local paths
	// =========================================================================
	static string MapToLocalPath(string relativePath)
	{
		if (relativePath.IndexOf("profile/") == 0)
		{
			if (!m_Settings.enableProfileSync)
				return "";
			return "$profile:" + relativePath.Substring(8, relativePath.Length() - 8);
		}
		if (relativePath.IndexOf("mission/") == 0)
		{
			if (!m_Settings.enableMissionSync)
				return "";
			return "$mission:" + relativePath.Substring(8, relativePath.Length() - 8);
		}
		if (relativePath.IndexOf("saves/") == 0)
		{
			if (!m_Settings.enableSavesSync)
				return "";
			return "$saves:" + relativePath.Substring(6, relativePath.Length() - 6);
		}
		return "";
	}

	// =========================================================================
	// Extension whitelist + script file blocking
	// =========================================================================
	static bool IsAllowedExtension(string path)
	{
		// Permit-all mode bypasses extension whitelist entirely
		if (m_Settings.permitAllExtensions)
			return true;

		// Block script files (.c) unless explicitly allowed
		if (m_Settings.blockScriptFiles)
		{
			if (path.Length() >= 2)
			{
				string lastTwo = path.Substring(path.Length() - 2, 2);
				if (lastTwo == ".c")
					return false;
			}
		}

		if (!m_Settings.allowedExtensions)
			return true;
		if (m_Settings.allowedExtensions.Count() == 0)
			return true;

		for (int i = 0; i < m_Settings.allowedExtensions.Count(); i++)
		{
			string ext = m_Settings.allowedExtensions[i];
			int extLen = ext.Length();
			if (path.Length() >= extLen)
			{
				string pathEnd = path.Substring(path.Length() - extLen, extLen);
				if (pathEnd == ext)
					return true;
			}
		}
		return false;
	}

	// =========================================================================
	// Binary file detection — routes through proxy-write to avoid corruption
	// =========================================================================
	static bool IsBinaryFile(string path)
	{
		if (!m_Settings.binaryExtensions)
			return false;

		for (int i = 0; i < m_Settings.binaryExtensions.Count(); i++)
		{
			string ext = m_Settings.binaryExtensions[i];
			int extLen = ext.Length();
			if (path.Length() >= extLen)
			{
				string pathEnd = path.Substring(path.Length() - extLen, extLen);
				if (pathEnd == ext)
					return true;
			}
		}
		return false;
	}

	// =========================================================================
	// URL encoding — handles spaces, parens, and other special chars in paths
	// =========================================================================
	static string UrlEncodePath(string path)
	{
		string result = "";
		for (int i = 0; i < path.Length(); i++)
		{
			string ch = path.Substring(i, 1);
			if (ch == " ")
				result = result + "%20";
			else if (ch == "(")
				result = result + "%28";
			else if (ch == ")")
				result = result + "%29";
			else if (ch == "[")
				result = result + "%5B";
			else if (ch == "]")
				result = result + "%5D";
			else if (ch == "{")
				result = result + "%7B";
			else if (ch == "}")
				result = result + "%7D";
			else if (ch == "#")
				result = result + "%23";
			else if (ch == "+")
				result = result + "%2B";
			else if (ch == "&")
				result = result + "%26";
			else if (ch == "?")
				result = result + "%3F";
			else if (ch == "=")
				result = result + "%3D";
			else
				result = result + ch;
		}
		return result;
	}

	// =========================================================================
	// Response validation — reject error pages, HTML, empty content
	// =========================================================================
	static bool IsValidContent(string content, string repoPath)
	{
		if (content == "")
		{
			Print(LOG_PREFIX + "Empty response for: " + repoPath);
			return false;
		}

		// Check for known error response patterns
		if (content.IndexOf(ERR_SERVER) == 0)
		{
			Print(LOG_PREFIX + "Server error response for: " + repoPath);
			return false;
		}
		if (content.IndexOf(ERR_CLIENT) == 0)
		{
			Print(LOG_PREFIX + "Client error response for: " + repoPath);
			return false;
		}
		if (content.IndexOf(ERR_TIMEOUT) == 0)
		{
			Print(LOG_PREFIX + "Request timed out for: " + repoPath);
			return false;
		}
		if (content.IndexOf(ERR_NOT_FOUND) == 0)
		{
			Print(LOG_PREFIX + "404 Not Found for: " + repoPath);
			return false;
		}
		if (content.IndexOf(ERR_HTML_OPEN) == 0)
		{
			Print(LOG_PREFIX + "Received HTML error page for: " + repoPath);
			return false;
		}
		if (content.IndexOf(ERR_HTML_TAG) == 0)
		{
			Print(LOG_PREFIX + "Received HTML error page for: " + repoPath);
			return false;
		}

		return true;
	}

	// =========================================================================
	// Download file via proxy — with retry and validation
	// =========================================================================
	static void DownloadAndWrite(string repoPath, string localPath, string sha, int sizeBytes)
	{
		// Binary files and large files use proxy-write to avoid corruption
		int thresholdBytes = m_Settings.proxyWriteThresholdKB * 1024;
		bool isBinary = IsBinaryFile(repoPath);
		bool isLarge = thresholdBytes > 0 && sizeBytes > thresholdBytes;
		if (isBinary || isLarge)
		{
			DownloadViaProxyWrite(repoPath, localPath, sha, sizeBytes);
			return;
		}

		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();

		string encodedPath = UrlEncodePath(repoPath);
		string rawUrl = m_Settings.proxyUrl + "/raw?owner=" + m_Settings.repoOwner + "&repo=" + m_Settings.repoName + "&branch=" + m_Settings.branch + "&path=" + encodedPath;
		if (m_Settings.githubToken != "")
			rawUrl = rawUrl + "&token=" + m_Settings.githubToken;

		int maxRetries = m_Settings.maxRetries;
		if (maxRetries < 1)
			maxRetries = 1;

		string content = "";
		bool success = false;

		for (int attempt = 1; attempt <= maxRetries; attempt++)
		{
			RestContext ctx = api.GetRestContext(rawUrl);
			content = ctx.GET_now("");

			if (IsValidContent(content, repoPath))
			{
				success = true;
				break;
			}

			if (attempt < maxRetries)
				Print(LOG_PREFIX + "Retry " + attempt.ToString() + "/" + maxRetries.ToString() + " for: " + repoPath);
		}

		if (!success)
		{
			Error(LOG_PREFIX + "FAILED after " + maxRetries.ToString() + " attempts: " + repoPath + " — file NOT written to prevent corruption.");
			m_FailedFiles++;
			return;
		}

		WriteFileContent(localPath, content, sha);
	}

	// =========================================================================
	// Proxy-write — proxy downloads from GitHub and writes directly to disk
	// Used for large/binary files that would timeout or corrupt via RestContext
	// =========================================================================
	static void DownloadViaProxyWrite(string repoPath, string localPath, string sha, int sizeBytes)
	{
		// Parse dest type and relative path from localPath
		string dest = "";
		string relPath = "";
		if (localPath.IndexOf("$profile:") == 0)
		{
			dest = "profile";
			relPath = localPath.Substring(9, localPath.Length() - 9);
		}
		else if (localPath.IndexOf("$mission:") == 0)
		{
			dest = "mission";
			relPath = localPath.Substring(9, localPath.Length() - 9);
		}
		else if (localPath.IndexOf("$saves:") == 0)
		{
			dest = "saves";
			relPath = localPath.Substring(7, localPath.Length() - 7);
		}

		if (dest == "" || relPath == "")
		{
			Error(LOG_PREFIX + "Cannot determine dest for proxy-write: " + localPath);
			m_FailedFiles++;
			return;
		}

		string encodedRepoPath = UrlEncodePath(repoPath);
		string encodedLocalPath = UrlEncodePath(relPath);
		string writeUrl = m_Settings.proxyUrl + "/write?owner=" + m_Settings.repoOwner + "&repo=" + m_Settings.repoName + "&branch=" + m_Settings.branch + "&path=" + encodedRepoPath + "&dest=" + dest + "&localpath=" + encodedLocalPath;
		if (m_Settings.githubToken != "")
			writeUrl = writeUrl + "&token=" + m_Settings.githubToken;

		Print(LOG_PREFIX + "Proxy-write " + (sizeBytes / 1048576).ToString() + "MB: " + repoPath);

		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();

		int maxRetries = m_Settings.maxRetries;
		if (maxRetries < 1)
			maxRetries = 1;

		string content = "";
		bool success = false;

		for (int attempt = 1; attempt <= maxRetries; attempt++)
		{
			RestContext ctx = api.GetRestContext(writeUrl);
			content = ctx.GET_now("");

			if (content.IndexOf("{\"ok\":true") == 0)
			{
				success = true;
				break;
			}

			if (attempt < maxRetries)
				Print(LOG_PREFIX + "Retry proxy-write " + attempt.ToString() + "/" + maxRetries.ToString() + " for: " + repoPath);
		}

		if (!success)
		{
			Error(LOG_PREFIX + "FAILED proxy-write after " + maxRetries.ToString() + " attempts: " + repoPath + " — response: " + content);
			m_FailedFiles++;
			return;
		}

		m_Settings.lastFileHashes.Set(localPath, sha);
		m_FileChanged = true;
		m_DownloadedFiles++;
		Print(LOG_PREFIX + "Updated (proxy-write): " + localPath);
	}

	// =========================================================================
	// Write file to disk
	// =========================================================================
	static void WriteFileContent(string path, string content, string sha)
	{
		CreateParentDirectories(path);
		FileHandle handle = OpenFile(path, FileMode.WRITE);
		if (handle == 0)
		{
			Error(LOG_PREFIX + "Cannot open for writing: " + path);
			m_FailedFiles++;
			return;
		}
		FPrint(handle, content);
		CloseFile(handle);

		m_Settings.lastFileHashes.Set(path, sha);
		m_FileChanged = true;
		m_DownloadedFiles++;
		Print(LOG_PREFIX + "Updated: " + path);
	}

	// =========================================================================
	// Deferred mission file processing (called from 5_Mission init)
	// =========================================================================
	static void ProcessDeferredFiles()
	{
		if (!m_Settings)
			return;
		if (!m_DeferredMissionFiles)
			return;

		int count = m_DeferredMissionFiles.Count();
		if (count == 0)
			return;

		Print(LOG_PREFIX + "Processing " + count.ToString() + " deferred mission file(s)...");

		string prefix = "";
		int prefixLen = 0;
		if (m_Settings.serverFolder != "")
		{
			prefix = m_Settings.serverFolder + "/";
			prefixLen = prefix.Length();
		}

		for (int i = 0; i < count; i++)
		{
			SVC_GitTreeEntry entry = m_DeferredMissionFiles[i];

			string relativePath = entry.path;
			if (prefixLen > 0)
				relativePath = entry.path.Substring(prefixLen, entry.path.Length() - prefixLen);

			string localPath = MapToLocalPath(relativePath);
			if (localPath == "")
				continue;

			// SHA cache check
			string cachedSha;
			if (m_Settings.lastFileHashes.Find(localPath, cachedSha))
			{
				if (cachedSha == entry.sha && FileExist(localPath))
				{
					m_SkippedFiles++;
					continue;
				}
			}

			DownloadAndWrite(entry.path, localPath, entry.sha, entry.size);
		}

		WaitForLfsDownloads();

		if (m_FileChanged)
			SaveConfig();

		Print(LOG_PREFIX + "Deferred mission sync complete. Downloaded: " + m_DownloadedFiles.ToString());
	}

	// =========================================================================
	// LFS download wait — polls /lfs-status until all background tasks finish
	// =========================================================================

	// Poll the proxy's /lfs-status endpoint until all LFS background downloads
	// have completed (pending == 0) or until we hit the poll limit.
	// The proxy sleeps 1 s server-side when pending > 0, so each GET_now() call
	// naturally paces itself; no Sleep() is needed here.
	// Max 600 polls ≈ 10 minutes ceiling before giving up.
	static void WaitForLfsDownloads()
	{
		if (!m_Settings)
			return;

		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();

		string statusUrl = m_Settings.proxyUrl + "/lfs-status";
		int maxPolls = 600;
		bool headerPrinted = false;
		bool transientLogged = false;
		int startTime = TickCount(0);

		RestContext ctx = api.GetRestContext(statusUrl);
		ctx.SetHeader("application/json");

		for (int i = 0; i < maxPolls; i++)
		{
			string response = ctx.GET_now("");

			int pending = ExtractJsonInt(response, "pending");

			// pending < 0 means parse failure — proxy may be an older build
			// without /lfs-status. Bail out silently on the first attempt.
			if (pending < 0)
			{
				if (i == 0)
					return;
				// Transient read failure mid-poll — log first occurrence, keep trying
				if (!transientLogged)
				{
					Print(LOG_PREFIX + "Transient /lfs-status read failure (poll " + (i + 1).ToString() + "), retrying...");
					transientLogged = true;
				}
				continue;
			}

			if (pending == 0)
			{
				if (headerPrinted)
				{
					int failed = ExtractJsonInt(response, "failed");
					if (failed > 0)
					{
						m_FailedFiles += failed;
						Error(LOG_PREFIX + "LFS downloads finished with " + failed.ToString() + " failure(s). Some large files may be incomplete.");
					}
					else
						Print(LOG_PREFIX + "All LFS downloads complete.");
				}
				return;
			}

			if (!headerPrinted)
			{
				Print(LOG_PREFIX + "Waiting for " + pending.ToString() + " LFS background download(s) to complete...");
				headerPrinted = true;
			}
			else
			{
				Print(LOG_PREFIX + "LFS still pending: " + pending.ToString() + " (poll " + (i + 1).ToString() + "/" + maxPolls.ToString() + ")");
			}
		}

		int elapsed = TickCount(startTime) / 10000;
		Error(LOG_PREFIX + "Timed out waiting for LFS downloads after " + (elapsed / 1000).ToString() + "s (" + maxPolls.ToString() + " polls). Some large files may be incomplete.");
	}

	// Extract an integer value from a minimal JSON string by key name.
	// Handles multi-digit values; returns -1 if the key is not found.
	static int ExtractJsonInt(string json, string key)
	{
		string search = "\"" + key + "\":";
		int pos = json.IndexOf(search);
		if (pos < 0)
			return -1;
		pos += search.Length();
		// Skip optional whitespace between ':' and the digit
		while (pos < json.Length())
		{
			string ws = json.Substring(pos, 1);
			if (ws == " " || ws == "\t")
				pos++;
			else
				break;
		}
		string numStr = "";
		while (pos < json.Length())
		{
			string ch = json.Substring(pos, 1);
			if (ch >= "0" && ch <= "9")
			{
				numStr += ch;
				pos++;
			}
			else
				break;
		}
		if (numStr == "")
			return -1;
		return numStr.ToInt();
	}

	// =========================================================================
	// Directory utilities
	// =========================================================================
	static void CreateDirectoryRecursive(string path)
	{
		path = TrimTrailingSlash(path);
		if (path == "" || FileExist(path))
			return;

		string parent = GetParentPath(path);
		if (parent != "")
			CreateDirectoryRecursive(parent);

		MakeDirectory(path);
	}

	static void CreateParentDirectories(string filePath)
	{
		string parent = GetParentPath(filePath);
		if (parent != "")
			CreateDirectoryRecursive(parent);
	}

	static string GetParentPath(string path)
	{
		int lastSlash = -1;
		for (int i = path.Length() - 1; i >= 0; i--)
		{
			string ch = path.Substring(i, 1);
			if (ch == "/" || ch == "\\")
			{
				lastSlash = i;
				break;
			}
		}
		if (lastSlash > 0)
			return path.Substring(0, lastSlash);
		return "";
	}

	static string TrimTrailingSlash(string path)
	{
		if (path.Length() == 0)
			return path;
		string last = path.Substring(path.Length() - 1, 1);
		if (last == "/" || last == "\\")
			return path.Substring(0, path.Length() - 1);
		return path;
	}
}

#endif
