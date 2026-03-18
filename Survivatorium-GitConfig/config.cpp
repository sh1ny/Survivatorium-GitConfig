class CfgPatches
{
	class Survivatorium_GitConfig
	{
		units[]={};
		weapons[]={};
		requiredVersion=0.1;
		requiredAddons[]={};
	};
};

class CfgMods
{
	class Survivatorium_GitConfig
	{
		dir="Survivatorium-GitConfig";
		picture="";
		action="";
		hideName=0;
		hidePicture=1;
		name="Survivatorium-GitConfig";
		author="Survivatorium";
		credits="Survivatorium";
		authorID="0";
		version="0.1.0";
		type="mod";
		dependencies[]=
		{
			"Game",
			"Mission"
		};
		class defs
		{
			class gameScriptModule
			{
				value="";
				files[]=
				{
					"Survivatorium-GitConfig/scripts/3_Game"
				};
			};
			class missionScriptModule
			{
				value="";
				files[]=
				{
					"Survivatorium-GitConfig/scripts/5_Mission"
				};
			};
		};
	};
};
