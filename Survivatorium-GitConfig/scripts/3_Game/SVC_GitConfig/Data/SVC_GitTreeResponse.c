// GitHub Git Trees API response structures
// Endpoint: GET /repos/{owner}/{repo}/git/trees/{branch}?recursive=1

class SVC_GitTreeEntry
{
	string path;
	string mode;
	string type;
	string sha;
	int size;
	string url;
}

class SVC_GitTreeResponse
{
	string sha;
	string url;
	ref array<ref SVC_GitTreeEntry> tree;
	bool truncated;
}
