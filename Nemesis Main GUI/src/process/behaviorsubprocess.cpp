#include <boost\thread.hpp>

#include "addanims.h"
#include "addevents.h"
#include "addvariables.h"
#include "behaviorgenerator.h"
#include "generator_utility.h"
#include "behaviorsubprocess.h"
#include "behaviorprocess_utility.h"

#include "functions\readtextfile.h"

#include "utilities\Terminator.h"

#include "add animation\import.h"
#include "add animation\templatetree.h"
#include "add animation\grouptemplate.h"
#include "add animation\singletemplate.h"
#include "add animation\playerexclusive.h"
#include "add animation\registeranimation.h"
#include "add animation\templateprocessing.h"

using namespace std;

extern Terminator* p_terminate;
extern atomic<int> m_RunningThread; 

atomic<int> extraCore = 0;

struct nodeJoint;

struct IDCatcher
{
private:
	int ID;
	int line;

public:
	IDCatcher(int id, int curline)
	{
		ID = id;
		line = curline;
	}

	int getID()
	{
		return ID;
	}

	int getLine()
	{
		return line;
	}
};

void BehaviorSub::BehaviorCompilation()
{
	try
	{
		++m_RunningThread;

		try
		{
			try
			{
				CompilingBehavior();
			}
			catch (exception& ex)
			{
				ErrorMessage(6002, filelist[curList], ex.what());
			}
		}
		catch (nemesis::exception&)
		{
			// resolved exception
		}
	}
	catch (...)
	{
		try
		{
			ErrorMessage(6002, filelist[curList], "Unknown");
		}
		catch (nemesis::exception&)
		{
			// resolved exception
		}
	}

	emit done();
}

void BehaviorSub::CompilingBehavior()
{
	ImportContainer exportID;

	string filepath = directory + filelist[curList];
	string behaviorFile = filelist[curList].substr(0, filelist[curList].find_last_of("."));
	string lowerBehaviorFile = boost::algorithm::to_lower_copy(behaviorFile);

	bool isFirstPerson = false;

	int lastID = 0;
	int curID = 0;
	int firstID = -1;

	unordered_map<string, bool> orievent;
	unordered_map<string, bool> orivariable;

	id eventid;
	id variableid;
	id attriid;
	id charid;

	unordered_map<int, unordered_map<string, vecstr>> clipAA;			// node id, original animation, list of alternative animations path
	unordered_map<string, vector<IDCatcher>> catcher;

	SSMap IDExist;
	map<int, vecstr> catalystMap;

	set<string> pceaMod;
	unordered_map<int, vector<PCEAData>*> pceaID;	// node ID, list of mods

													// final output
	string outputdir;
	string ZeroEvent;
	string ZeroVariable;

#ifdef DEBUG
	if (modID.length() > 0)
	{
		outputdir = "new_behaviors\\" + GetFileDirectory(behaviorPath[lowerBehaviorFile].substr(behaviorPath[lowerBehaviorFile].find("\\") + 1)) + modID +
			lowerBehaviorFile;
	}
	else
	{
		outputdir = "new_behaviors\\" + behaviorPath[lowerBehaviorFile].substr(behaviorPath[lowerBehaviorFile].find("\\") + 1);
	}
#else
	if (modID.length() > 0) outputdir = GetFileDirectory(behaviorPath[lowerBehaviorFile]) + modID + lowerBehaviorFile;
	else outputdir = behaviorPath[lowerBehaviorFile];
#endif
	if (error) throw nemesis::exception();

	DebugLogging("Processing behavior: " + filepath);
	DebugLogging("Behavior output path: " + outputdir);
	emit progressAdd();

	if (modID.length() > 0 && isFileExist(outputdir + ".hkx"))
	{
		int i = 0;

		while (i < 9)
		{
			emit progressAdd();
			++i;
		}

		return;
	}

	double duration;

	{
		bool hasAA = false;
		bool isOpen = true;
		bool special = false;
		bool newBone = false;

		int counter = 0;
		int oribone = -1;
		int bonenum = -1;
		int eventelements = -1;
		int variableelements = -1;
		int attributeelements = -1;
		int characterelements = -1;
		size_t elementLine = -1;
		size_t openRange = 0;

		unordered_map<int, string> eventName;
		unordered_map<int, string> varName;
		unordered_map<int, string> attriName;
		unordered_map<int, string> charName;

		unordered_map<string, string> chosenLines;

		string newMod;

		set<string> AAGroupList;
		set<string> AAEventName;

		if (lowerBehaviorFile.find("_1stperson") != NOT_FOUND)
		{
			isFirstPerson = true;
		}

		if (alternateAnim.size() != 0)
		{
			hasAA = true;
		}

		// read behavior file
		vecstr catalyst;
		vecstr modLine;

		if (!boost::filesystem::is_directory(filepath))
		{
			size_t size = fileLineCount(filepath);
			catalyst.reserve(size);
			modLine.reserve(size);
			FileReader BehaviorFormat(filepath);

			if (BehaviorFormat.GetFile())
			{
				string line;

				while (BehaviorFormat.GetLines(line))
				{
					bool skip = false;

					if (line.find("<!-- ", 0) != NOT_FOUND)
					{
						if (line.find("<!-- NEW *", 0) != NOT_FOUND)
						{
							size_t tempint = line.find("<!-- NEW *", 0) + 10;
							string mod = line.substr(tempint, line.find("* -->", tempint + 1) - tempint);

							if (!chosenBehavior[mod]) isOpen = false;
							else newMod = mod;

							skip = true;
						}
						else if (line.find("<!-- NEW ^", 0) != NOT_FOUND || line.find("<!-- FOREACH ^") != NOT_FOUND)
						{
							special = true;
						}
						else if (line.find("<!-- CLOSE -->", 0) != NOT_FOUND)
						{
							if (!special)
							{
								newMod.clear();
								skip = true;
							}

							isOpen = true;
							special = false;
						}
					}

					if (isOpen && !skip)
					{
						if (line == "	</hksection>") break;

						while (true)
						{
							if (line.find("<!-- *", 0) != NOT_FOUND)
							{
								size_t tempint = line.find("<!-- *") + 6;
								string mod = line.substr(tempint, line.find("* -->", tempint + 1) - tempint);
								chosenLines[mod] = line;
								break;
							}
							else if (line.find("<!-- original -->", 0) != NOT_FOUND)
							{
								if (chosenLines.size() != 0)
								{
									line = behaviorLineChooser(line, chosenLines, behaviorPriority);
									chosenLines.clear();
								}
								else
								{
									ErrorMessage(1165);
								}
							}

							size_t pos = line.find("<hkobject name=\"");

							if (pos != NOT_FOUND && line.find("signature=\"", pos) != NOT_FOUND)
							{
								string ID = boost::regex_replace(string(line),
									boost::regex(".*<hkobject name=\"#([0-9]+)\" class=\"[a-zA-Z]+\" signature=\".*\">.*"), string("\\1"));

								if (ID != line)
								{
									curID = stoi(ID);

									if (curID >= lastID) lastID = curID + 1;
								}
							}

							modLine.push_back(newMod);
							catalyst.push_back(line);
							break;
						}
					}

					if (error) throw nemesis::exception();
				}
			}
			else
			{
				ErrorMessage(3002, filepath);
			}
		}
		else
		{
			ErrorMessage(3001, filepath);
		}

		if (catalyst.size() != 0 && catalyst.back().length() != 0 && catalyst.back().find("<!-- CONDITION END -->") == NOT_FOUND &&
			catalyst.back().find("<!-- CLOSE -->") == NOT_FOUND)
		{
			modLine.push_back(newMod);
			catalyst.push_back("");
		}

		DebugLogging("Processing behavior: " + filepath + " (Check point 1, File extraction & mod selection complete)");

		if (isCharacter)
		{
			DebugLogging("Processing behavior: " + filepath + " (IsCharater: TRUE)");
			string rigfile = "<hkparam name=\"rigName\">";
			string bonemap = "<hkparam name=\"bonePairMap\" numelements=\"";
			bool found = false;

			for (string& line : catalyst)
			{
				if (line.find(bonemap) != NOT_FOUND)
				{
					size_t pos = line.find(bonemap) + bonemap.length();
					oribone = stoi(line.substr(pos, line.find("\">", pos) - pos));
				}
				else if (line.find(rigfile) != NOT_FOUND)
				{
					size_t pos = line.find(rigfile) + rigfile.length();
					rigfile = line.substr(pos, line.find("</hkparam>", pos) - pos);
					found = true;
					break;
				}
			}

			boost::filesystem::path curFile(GetFileDirectory(outputdir));
			rigfile = curFile.parent_path().parent_path().string() + "\\" + rigfile;

			if (found && isFileExist(rigfile) && !boost::filesystem::is_directory(rigfile))
			{
				if (SSE)
				{
					FILE* bonefile;
					fopen_s(&bonefile, rigfile.c_str(), "r+b");

					if (bonefile)
					{
						int num = 0;
						vector<char> chlist;
						bool start = false;
						bool startCount = false;

						while (!feof(bonefile))
						{
							chlist.push_back(fgetc(bonefile));
						}

						fclose(bonefile);

						for (unsigned int i = 0; i < chlist.size(); ++i)
						{
							if (i % 16 == 0 && chlist[i] == unsigned char(0x4E) && chlist[i + 1] == unsigned char(0x50) && chlist[i + 2] == unsigned char(0x43)
								&& chlist[i + 3] == unsigned char(0x20) && chlist[i + 4] == unsigned char(0x52) && chlist[i + 5] == unsigned char(0x6F)
								&& chlist[i + 6] == unsigned char(0x6F) && chlist[i + 7] == unsigned char(0x74) && chlist[i + 8] == unsigned char(0x20)
								&& chlist[i + 9] == unsigned char(0x5B) && chlist[i + 10] == unsigned char(0x52) && chlist[i + 11] == unsigned char(0x6F)
								&& chlist[i + 12] == unsigned char(0x6F) && chlist[i + 13] == unsigned char(0x74) && chlist[i + 14] == unsigned char(0x5D)
								&& chlist[i + 15] == unsigned char(0x0) && chlist[i + 16] == '�' && chlist[i + 17] == '�')
							{
								start = true;
							}
							else if (start && i % 16 == 0)
							{
								bool pass = true;

								for (unsigned int j = i; j < i + 16; ++j)
								{
									if (chlist[j] != unsigned char(0x0) && chlist[j] != unsigned char(0x1))
									{
										pass = false;
										break;
									}
								}

								if (pass)
								{
									startCount = true;
									start = false;
									i += 15;
									++num;
								}
							}
							else if (startCount)
							{
								if (chlist[i] != unsigned char(0x0)) break;

								i += 15;
								++num;
							}

							if (error) throw nemesis::exception();
						}

						if (oribone < num)
						{
							bonenum = num - oribone;
							newBone = true;
						}
					}
					else
					{
						ErrorMessage(3002, rigfile);
					}
				}
				else
				{
					vecstr storeline;
					hkxcmdXmlInput(rigfile.substr(0, rigfile.find_last_of(".")), storeline);
					bonemap = "<hkparam name=\"parentIndices\" numelements=\"";

					for (auto& line : storeline)
					{
						if (line.find(bonemap) != NOT_FOUND)
						{
							size_t pos = line.find(bonemap) + bonemap.length();
							int num = stoi(line.substr(pos, line.find("\">", pos) - pos));

							if (oribone < num)
							{
								bonenum = num - oribone;
								newBone = true;
								break;
							}
						}
					}
				}

				if (error) throw nemesis::exception();
			}

			DebugLogging("Processing behavior: " + filepath + " (Check point 1.5, Character bone identification complete)");
		}
		else
		{
			DebugLogging("Processing behavior: " + filepath + " (IsCharater: FALSE)");
		}

		if (error) throw nemesis::exception();

		emit progressAdd();

		bool isClip = false;
		bool negative = false;
		string curNum;
		unordered_map<string, int> stateStrID;					// parent node ID, new state ID counter; counter for state ID
		unordered_map<string, string> registeredStateID;		// parent node ID & base ID & modcode & ID, existing state ID
		unordered_map<string, bool> isAdded;

		for (auto& line : catalyst)
		{
			size_t pos = line.find("<hkobject name=\"", 0);

			if (pos != NOT_FOUND && line.find("signature=\"", 0) != NOT_FOUND)
			{
				pos += 16;
				string nodeID = line.substr(pos, line.find("\" class=\"", pos));

				if (isOnlyNumber(nodeID))
				{
					curID = stoi(nodeID);
				}
				else
				{
					if (nodeID.find("$", 0) != NOT_FOUND)
					{
						string mod = nodeID.substr(0, nodeID.find("$"));

						if (chosenBehavior[mod])
						{
							string ID = to_string(lastID);

							while (ID.length() < 4)
							{
								ID = "0" + ID;
							}

							line.replace(line.find(nodeID), nodeID.length(), ID);
							IDExist[nodeID] = ID;
							curID = lastID;
							++lastID;

							if (lastID == 9216) ++lastID;
						}
						else if (mod == "MID")
						{
							ErrorMessage(1020);
						}
					}
				}

				isClip = line.find("class=\"hkbClipGenerator\" signature=\"") != NOT_FOUND;
			}
			else if (isClip)
			{
				pos = line.find("<hkparam name=\"animationName\">");

				if (pos != NOT_FOUND)
				{
					pos += 30;
					string animPath = line.substr(pos, line.find("</hkparam>", pos) - pos);
					boost::algorithm::to_lower(animPath);
					addUsedAnim(lowerBehaviorFile, animPath);
					string animFile = GetFileName(animPath) + ".hkx";

					if (!isFirstPerson)
					{
						auto replacer = animReplaced.find(animFile);

						if (replacer != animReplaced.end() && replacer->second.size())
						{
							pceaID.insert(make_pair(curID, &replacer->second));

							for (auto& data : replacer->second)
							{
								pceaMod.insert(data.modFile);
							}
						}
					}

					// Dynamic AA function without a template
					if (hasAA)
					{
						string tempAnimFile = isFirstPerson ? animFile + "_1p*" : animFile;
						auto aa = alternateAnim.find(tempAnimFile);

						if (aa != alternateAnim.end())
						{
							clipAA[curID][animFile] = aa->second;
							AAGroupList.insert(isFirstPerson ? AAGroup[animFile] + "_1p*" : AAGroup[animFile]);
							auto aaEvent = AAHasEvent.find(tempAnimFile);

							if (aaEvent != AAHasEvent.end())
							{
								for (string& AA_animName : aaEvent->second)
								{
									for (unsigned int k = 0; k < AAEvent[AA_animName].size(); ++k)
									{
										if (k % 2 == 0) AAEventName.insert(AAEvent[AA_animName][k]);
									}
								}
							}
						}
					}
				}
			}

			if (error) throw nemesis::exception();
		}

		DebugLogging("Processing behavior: " + filepath + " (Check point 2, ID replacement & PCEA record complete)");

		{
			size_t pos = catalyst[1].find("toplevelobject=");

			if (pos == NOT_FOUND)
			{
				for (unsigned int l = 0; l < catalyst.size(); ++l)
				{
					if (l > 50) break;

					string line = catalyst[l];

					pos = line.find("toplevelobject=");

					if (pos != NOT_FOUND)
					{
						firstID = stoi(boost::regex_replace(string(line.substr(pos)), boost::regex("[^0-9]*([0-9]+).*"), string("\\1")));
						break;
					}
				}

				if (firstID == -1) ErrorMessage(1095, filepath);
			}
			else
			{
				firstID = stoi(boost::regex_replace(string(catalyst[1].substr(pos)), boost::regex("[^0-9]*([0-9]+).*"), string("\\1")));
			}
		}

		curID = firstID;
		bool doneEventName = false;
		bool doneEventInfo = false;
		bool doneAttributeName = false;
		bool doneAttributeDef = false;
		bool doneCharacProName = false;
		bool doneCharacProInfo = false;
		bool doneVarName = false;
		bool doneVarWord = false;
		bool doneVarInfo = false;
		bool doneAnimName = false;
		bool doneDeform = false;
		bool doneRigid = false;
		bool doneAnimFileName = false;
		bool isClipTrigger = false;
		bool replacedNum = false;

		bool eventOpen = false;
		bool varOpen = false;
		bool attriOpen = false;
		bool charOpen = false;
		bool animOpen = false;
		bool fp_animOpen = false;
		bool otherAnimOpen = false;
		bool norElement = false;
		bool characterAA = false;

		// add picked behavior and remove not picked behavior
		for (unsigned int l = 0; l < catalyst.size(); ++l)
		{
			bool elementCatch = false;
			string line = catalyst[l];
			boost::smatch check;

			if (line.length() == 0)
			{
				catalystMap[curID].push_back(line);
			}
			else
			{
				newMod = modLine[l];

				if (line.find("<!-- ", 0) != NOT_FOUND)
				{
					if (line.find("<!-- NEW ^", 0) != NOT_FOUND || line.find("<!-- FOREACH ^") != NOT_FOUND) special = true;
					else if (line.find("<!-- CLOSE -->", 0) != NOT_FOUND) special = false;
				}

				if (line.find("<hkobject name=\"", 0) != NOT_FOUND && line.find("signature=\"", 0) != NOT_FOUND)
				{
					isClipTrigger = false;
					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkobject name=\"#(.*)\" class=\"([a-zA-Z]+)\" signature=\".*\">")))
					{
						curID = stoi(match[1]);

						if (match[2] == "hkbClipTriggerArray") isClipTrigger = true;
					}
				}

				if (line.find("$", line.find("#")) != NOT_FOUND)
				{
					if (line.find(">#") != NOT_FOUND)
					{
						size_t reference = count(line.begin(), line.end(), '#');
						size_t nextpos = 0;

						for (size_t k = 0; k < reference; ++k)
						{
							nextpos = line.find("#", nextpos) + 1;
							string numID = boost::regex_replace(string(line.substr(nextpos)), boost::regex("[^0-9]*([0-9]+).*"), string("\\1"));
							string ID = line.substr(nextpos, line.find(numID) - nextpos + numID.length());

							if (line.find(ID, 0) != NOT_FOUND && ID.find("$") != NOT_FOUND)
							{
								if (IDExist[ID].length() != 0)
								{
									boost::smatch match;
									boost::regex_match(line, match, boost::regex(".*#(" + ID.substr(0, ID.find("$")) + "[$]" + numID + ")[^0-9]*.*"));
									nextpos = match.position(1);
									line.replace(nextpos, ID.length(), IDExist[ID]);
								}
								else if (!special)
								{
									IDCatcher catchingID(curID, int(catalystMap[curID].size()));
									catcher[ID].push_back(catchingID);
								}
							}
						}
					}
					else if (line.find("\t\t\t#") != NOT_FOUND)
					{
						vecstr generator;
						StringSplit(line, generator);
						line.append("%");

						for (unsigned int p = 0; p < generator.size(); p++)
						{
							string ID = generator[p];
							string numID = boost::regex_replace(string(ID), boost::regex("[^0-9]*([0-9]+).*"), string("\\1"));

							if (ID.find("$") != NOT_FOUND)
							{
								string masterFormat = ID.substr(1, ID.find("$") - 1);

								if (ID == "#" + masterFormat + "$" + numID && line.find(ID, 0) != NOT_FOUND)
								{
									ID = ID.substr(1, ID.length() - 1);

									if (IDExist[ID].length() != 0)
									{
										boost::smatch match;
										boost::regex_match(line, match, boost::regex(".*#(" + masterFormat + "[$]" + numID + ")[^0-9]+.*"));
										line.replace(match.position(1), ID.length(), IDExist[ID]);
									}
									else if (!special)
									{
										IDCatcher catchingID(curID, int(catalystMap[curID].size()));
										catcher[ID].push_back(catchingID);
									}
								}
							}
						}

						line.pop_back();
					}
				}

				// get event id from newAnimation
				if ((!doneEventName && line.find("<hkparam name=\"eventNames\" numelements=", 0) != NOT_FOUND) ||
					(!doneEventInfo && line.find("<hkparam name=\"eventInfos\" numelements=", 0) != NOT_FOUND))
				{
					line.find("<hkparam name=\"eventNames\" numelements=", 0) != NOT_FOUND ? doneEventName = true : doneEventInfo = true;

					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkparam name=\"(.+?)\" numelements=\"(.+?)\">")))
					{
						if (eventelements == -1)
						{
							if (line.find("</hkparam>") == NOT_FOUND)
							{
								counter = 0;
								eventOpen = true;
								elementCatch = true;
								string templine = line.substr(0, match.position(1));
								openRange = count(templine.begin(), templine.end(), '\t');
								curNum = match[1];
							}
						}
						else
						{
							string templine = line.substr(0, match.position(1));
							openRange = count(templine.begin(), templine.end(), '\t');
							curNum = match[1];
							replacedNum = true;
							eventOpen = true;
							line.replace(match.position(2), match[2].length(), to_string(eventelements));
						}
					}
				}
				else if ((!doneAttributeName && line.find("<hkparam name=\"attributeNames\" numelements=") != NOT_FOUND) ||
					(!doneAttributeDef && line.find("<hkparam name=\"attributeDefaults\" numelements=") != NOT_FOUND))
				{
					line.find("<hkparam name=\"attributeNames\" numelements=") != NOT_FOUND ? doneAttributeName = true : doneAttributeDef = true;

					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkparam name=\"(.+?)\" numelements=\"(.+?)\">")))
					{
						if (attributeelements == -1)
						{
							if (line.find("</hkparam>") == NOT_FOUND)
							{
								counter = 0;
								attriOpen = true;
								elementCatch = true;
								string templine = line.substr(0, match.position(1));
								openRange = count(templine.begin(), templine.end(), '\t');
								curNum = match[1];
							}
						}
						else
						{
							line.replace(match.position(2), match[2].length(), to_string(attributeelements));
						}
					}
				}
				else if ((!doneCharacProName && line.find("<hkparam name=\"characterPropertyNames\" numelements=") != NOT_FOUND) ||
					(!doneCharacProInfo && line.find("<hkparam name=\"characterPropertyInfos\" numelements=") != NOT_FOUND))
				{
					line.find("<hkparam name=\"characterPropertyNames\" numelements=") != NOT_FOUND ? doneCharacProName = true : doneCharacProInfo = true;

					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkparam name=\"(.+?)\" numelements=\"(.+?)\">")))
					{
						if (characterelements == -1)
						{
							if (line.find("</hkparam>") == NOT_FOUND)
							{
								counter = 0;
								charOpen = true;
								elementCatch = true;
								string templine = line.substr(0, match.position(1));
								openRange = count(templine.begin(), templine.end(), '\t');
								curNum = match[1];
							}
						}
						else
						{
							line.replace(match.position(2), match[2].length(), to_string(characterelements));
						}
					}
				}
				else if ((!doneVarName && line.find("<hkparam name=\"variableNames\" numelements=") != NOT_FOUND) ||
					(!doneVarWord && line.find("<hkparam name=\"wordVariableValues\" numelements=") != NOT_FOUND) ||
					(!doneVarInfo && line.find("<hkparam name=\"variableInfos\" numelements=") != NOT_FOUND))
				{
					line.find("<hkparam name=\"variableNames\" numelements=") != NOT_FOUND ? doneVarName = true :
						line.find("<hkparam name=\"wordVariableValues\" numelements=") != NOT_FOUND ? doneVarWord = true : doneVarInfo = true;

					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkparam name=\"(.+?)\" numelements=\"(.+?)\">")))
					{
						if (variableelements == -1)
						{
							if (line.find("</hkparam>") == NOT_FOUND)
							{
								counter = 0;
								varOpen = true;
								elementCatch = true;
								string templine = line.substr(0, match.position(1));
								openRange = count(templine.begin(), templine.end(), '\t');
								curNum = match[1];
							}
						}
						else
						{
							replacedNum = true;
							varOpen = true;
							string templine = line.substr(0, match.position(1));
							openRange = count(templine.begin(), templine.end(), '\t');
							curNum = match[1];
							line.replace(match.position(2), match[2].length(), to_string(variableelements));
						}
					}
				}
				else if (!doneAnimName && line.find("<hkparam name=\"animationNames\" numelements=\"") != NOT_FOUND)
				{
					if (filelist[curList].find("_1stperson\\") != NOT_FOUND) fp_animOpen = true;

					doneAnimName = true;
					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkparam name=\"(.+?)\" numelements=\"(.+?)\">")))
					{
						if (line.find("</hkparam>") == NOT_FOUND)
						{
							counter = 0;
							animOpen = true;
							elementCatch = true;
							string templine = line.substr(0, match.position(1));
							openRange = count(templine.begin(), templine.end(), '\t');
							string number = match[2];
							curNum = match[1];

							if (!isOnlyNumber(number)) ErrorMessage(1174, filepath, l + 1, line.substr(line.find("<hkparam name=\"animationNames\" numelements=\"")));

							base = stoi(number);
						}
					}
				}
				else if ((!doneDeform && line.find("<hkparam name=\"deformableSkinNames\" numelements=\"") != NOT_FOUND) ||
					(!doneRigid && line.find("<hkparam name=\"rigidSkinNames\" numelements=\"") != NOT_FOUND) ||
					(!doneAnimFileName && line.find("<hkparam name=\"animationFilenames\" numelements=\"") != NOT_FOUND))
				{
					line.find("<hkparam name=\"deformableSkinNames\" numelements=\"") != NOT_FOUND ? doneDeform = true :
						line.find("<hkparam name=\"rigidSkinNames\" numelements=\"") != NOT_FOUND ? doneRigid = true : doneAnimFileName = true;

					nemesis::smatch match;

					if (nemesis::regex_search(line, match, boost::regex("<hkparam name=\"(.+?)\" numelements=\"(.+?)\">")))
					{
						if (line.find("</hkparam>") == NOT_FOUND)
						{
							counter = 0;
							otherAnimOpen = true;
							elementCatch = true;
							string templine = line.substr(0, match.position(1));
							openRange = count(templine.begin(), templine.end(), '\t');
							curNum = match[1];
						}
					}
				}
				else if (boost::regex_search(line, check, boost::regex("<hkparam name=\"(.+?)\" numelements=\"")) && line.find("</hkparam>", check.position(1)) == NOT_FOUND)
				{
					counter = 0;
					norElement = true;
					elementCatch = true;
					string templine = line.substr(0, check.position(1));
					openRange = count(templine.begin(), templine.end(), '\t');
				}
				// counting for numelement
				else if (eventOpen)
				{
					size_t pos = line.find("</hkparam>");

					if (pos != NOT_FOUND)
					{
						string templine = line.substr(0, pos);
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							unordered_map<string, bool> isExist;

							for (auto& aaEvent : AAEventName)
							{
								AddEvents(curNum, catalystMap[curID], aaEvent, orievent, isExist, counter, ZeroEvent, eventid, eventName);
							}

							setstr codelist = BehaviorTemplate->grouplist[lowerBehaviorFile];

							for (auto& templatecode : codelist)
							{
								for (auto& newEvent : newAnimEvent[templatecode + modID])
								{
									AddEvents(curNum, catalystMap[curID], newEvent, orievent, isExist, counter, ZeroEvent, eventid, eventName);
								}
							}

							if (eventelements == -1) eventelements = counter;

							eventOpen = false;
							replacedNum ? replacedNum = false : elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else if (!replacedNum)
					{
						pos = line.find("<hkcstring>");

						if (pos != NOT_FOUND)
						{
							pos += 11;
							string name = line.substr(pos, line.find("</hkcstring>", pos) - pos);
							eventName[counter] = name;
							eventid[name] = counter;
							orievent[name] = true;

							if (counter == 0) ZeroEvent = name;

							++counter;
						}
						else if (line.find("<hkparam name=\"flags\">") != NOT_FOUND)
						{
							++counter;
						}
					}
				}
				else if (varOpen)
				{
					size_t pos = line.find("</hkparam>");

					if (pos != NOT_FOUND)
					{
						string templine = line.substr(0, pos);
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							unordered_map<string, bool> isExist;

							for (auto& AAVariable : AAGroupList)
							{
								AddVariables(curNum, catalystMap[curID], "Nemesis_AA" + AAVariable, orivariable, isExist, counter, ZeroVariable, variableid, varName);
								AddVariables(curNum, catalystMap[curID], "Nemesis_AA_Priority" + AAVariable, orivariable, isExist, counter, ZeroVariable, variableid, varName);

								for (int k = 0; k < AAgroup_Counter[AAVariable]; ++k)
								{
									AddVariables(curNum, catalystMap[curID], "Nemesis_AA_Priority" + AAVariable + "_" + to_string(k), orivariable, isExist, counter,
										ZeroVariable, variableid, varName);
								}
							}

							setstr codelist = BehaviorTemplate->grouplist[lowerBehaviorFile];

							for (auto& templatecode : codelist)
							{
								for (auto& newVariable : newAnimVariable[templatecode + modID])
								{
									AddVariables(curNum, catalystMap[curID], newVariable, orivariable, isExist, counter, ZeroVariable, variableid, varName);
								}
							}

							for (auto& modname : pceaMod)
							{
								AddVariables(curNum, catalystMap[curID], modname, orivariable, isExist, counter, ZeroVariable, variableid, varName);
							}

							if (variableelements == -1) variableelements = counter;

							varOpen = false;
							replacedNum ? replacedNum = false : elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else if (!replacedNum)
					{
						size_t pos = line.find("<hkcstring>");

						if (curNum == "variableNames" && pos != NOT_FOUND)
						{
							pos += 11;
							string name = line.substr(pos, line.find("</hkcstring>", pos) - pos);
							varName[counter] = name;
							variableid[name] = counter;
							orivariable[name] = true;

							if (counter == 0) ZeroVariable = name;

							++counter;
						}
						else if (curNum == "wordVariableValues" && line.find("<hkparam name=\"value\">") != NOT_FOUND)
						{
							++counter;
						}
						else if (curNum == "variableInfos" && line.find("<hkparam name=\"type\">") != NOT_FOUND)
						{
							++counter;
						}
					}
				}
				else if (attriOpen)
				{
					size_t pos = line.find("</hkparam>");

					if (pos != NOT_FOUND)
					{
						string templine = line.substr(0, pos);
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							if (attributeelements == -1) attributeelements = counter;

							attriOpen = false;
							elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else if (line.find("<hkcstring>") != NOT_FOUND)
					{
						size_t nextpos = line.find("<hkcstring>") + 11;
						string name = line.substr(nextpos, line.find("</hkcstring>", nextpos) - nextpos);
						attriName[counter] = name;
						attriid[name] = counter;
						++counter;
					}
				}
				else if (charOpen)
				{
					if (line.find("</hkparam>") != NOT_FOUND)
					{
						string templine = line.substr(0, line.find("</hkparam>"));
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							if (characterelements == -1) characterelements = counter;

							charOpen = false;
							elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else
					{
						size_t pos = line.find("<hkcstring>");

						if (pos != NOT_FOUND)
						{
							pos += 11;
							string name = line.substr(pos, line.find("</hkcstring>", pos) - pos);
							charName[counter] = name;
							charid[name] = counter;
							++counter;
						}
					}
				}
				else if (animOpen)
				{
					size_t pos = line.find("</hkparam>");

					if (pos != NOT_FOUND)
					{
						string templine = line.substr(0, pos);
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							for (auto it = BehaviorTemplate->grouplist.begin(); it != BehaviorTemplate->grouplist.end(); ++it)
							{
								for (unsigned int k = 0; k < behaviorJoints[it->first].size(); ++k)
								{
									if (lowerBehaviorFile != behaviorJoints[it->first][k]) continue;

									for (auto& templatecode : it->second)
									{
										if (BehaviorTemplate->optionlist[templatecode].core) continue;

										for (unsigned int k = 0; k < newAnimation[templatecode].size(); ++k)
										{
											if (!newAnimation[templatecode][k]->isKnown())
											{
												bool addAnim = false;
												string animPath = "Animations\\" + newAnimation[templatecode][k]->GetFilePath();
												AddAnims(line, animPath, outputdir, behaviorFile, lowerBehaviorFile, newMod, catalystMap[curID], counter, isAdded, addAnim);

												if (addAnim) addAnimation();
											}
										}
									}
								}
							}

							if (!isFirstPerson)
							{
								for (auto& AA : alternateAnim)
								{
									for (auto& anim : AA.second)
									{
										if (anim != "x")
										{
											bool addAnim = false;
											string animPath = "Animations\\" + anim;
											AddAnims(line, animPath, outputdir, behaviorFile, lowerBehaviorFile, newMod, catalystMap[curID], counter, isAdded, addAnim);

											if (addAnim) addAnimation();
										}
									}
								}

								for (auto& pcea : pcealist)
								{
									for (auto& animPath : pcea.animPathList)
									{
										bool addAnim = false;
										AddAnims(line, animPath.second, outputdir, behaviorFile, lowerBehaviorFile, newMod, catalystMap[curID], counter, isAdded, addAnim);

										if (addAnim) addAnimation();
									}
								}
							}

							animOpen = false;
							fp_animOpen = false;
							elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else
					{
						pos = line.find("<hkcstring>");

						if (pos != NOT_FOUND)
						{
							pos += 11;
							string animPath = line.substr(pos, line.find("</hkcstring>", pos) - pos);
							string animFile = GetFileName(animPath) + ".hkx";

							if (!fp_animOpen && !characterAA && alternateAnim.find(boost::to_lower_copy(animFile)) != alternateAnim.end())
							{
								if (!isCharacter) ErrorMessage(1184, behaviorFile);

								characterAA = true;
							}

							if (activatedBehavior["gender*"])
							{
								if (lowerBehaviorFile == "defaultfemale")
								{
									if (!boost::iequals(animPath, "Animations\\female\\" + animFile))
									{
										boost::filesystem::path animation(GetFileDirectory(outputdir));

										if (isFileExist(animation.parent_path().parent_path().string() + "\\Animations\\female\\" + animFile))
										{
											line.replace(pos, animPath.length(), "Animations\\female\\" + animFile);
										}
										else if (boost::iequals(animPath, "Animations\\male\\" + animFile))
										{
											if (isFileExist(animation.parent_path().parent_path().string() + "\\Animations\\" + animFile))
											{
												line.replace(pos, animPath.length(), "Animations\\" + animFile);
											}
										}
									}
								}
								else if (lowerBehaviorFile == "defaultmale" && !boost::iequals(animPath, "Animations\\male\\" + animFile))
								{
									boost::filesystem::path animation(GetFileDirectory(outputdir));

									if (isFileExist(animation.parent_path().parent_path().string() + "\\Animations\\male\\" + animFile))
									{
										line.replace(pos, animPath.length(), "Animations\\male\\" + animFile);
									}
									else if (boost::iequals(animPath, "Animations\\female\\" + animFile))
									{
										if (isFileExist(animation.parent_path().parent_path().string() + "\\Animations\\" + animFile))
										{
											line.replace(pos, animPath.length(), "Animations\\" + animFile);
										}
									}
								}
							}

							boost::to_lower(animPath);
							boost::to_lower(animFile);
							isAdded[animPath] = true;
							registeredAnim[lowerBehaviorFile][animFile] = true;
							addAnimation();
							++counter;

							if (newMod.length() == 0) newMod = "Skyrim";

							vector<set<string>>* match_ptr = &animModMatch[lowerBehaviorFile][animFile];
							size_t matchSize = match_ptr->size();

							if (matchSize == 0)
							{
								match_ptr->push_back(set<string>{animPath});
								match_ptr->push_back(set<string>{newMod});
							}
							else if (matchSize == 2)
							{
								match_ptr->at(0).insert(animPath);
								match_ptr->at(1).insert(newMod);
							}
							else
							{
								ErrorMessage(1058);
							}
						}
					}
				}
				else if (otherAnimOpen)
				{
					size_t pos = line.find("</hkparam>");

					if (pos != NOT_FOUND)
					{
						string templine = line.substr(0, pos);
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							otherAnimOpen = false;
							elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else if (line.find("<hkcstring>") != NOT_FOUND)
					{
						++counter;
					}
				}
				else if (norElement)
				{
					size_t pos = line.find("</hkparam>");

					if (pos != NOT_FOUND)
					{
						string templine = line.substr(0, pos);
						size_t range = count(templine.begin(), templine.end(), '\t');

						if (openRange == range)
						{
							norElement = false;
							elementUpdate(elementLine, counter, curID, catalystMap);
						}
					}
					else
					{
						pos = line.find("<hkobject>");

						if (pos != NOT_FOUND)
						{
							string templine = line.substr(0, pos);
							size_t range = count(templine.begin(), templine.end(), '\t');

							if (range == openRange + 1) ++counter;
						}
						else if (line.find("\t\t\t#") != NOT_FOUND)
						{
							string templine = line.substr(0, line.find("#"));
							size_t reference = count(templine.begin(), templine.end(), '\t');

							if (reference == openRange + 1)
							{
								int number = int(count(line.begin(), line.end(), '#'));
								counter += number;
							}
						}
					}
				}

				if (error) throw nemesis::exception();

				int repeatnum = 1;

				if (line.find("$") != NOT_FOUND)
				{
					size_t pos = line.find("<!-- Bone$N -->");

					if (pos != NOT_FOUND && pos == line.length() - 15)
					{
						if (newBone) repeatnum = bonenum;
						else repeatnum = 0;
					}

					if (!ignoreFunction[filelist[curList]][curID])
					{
						if (line.find("$eventID[", 0) != NOT_FOUND &&  line.find("]$", 0) != NOT_FOUND)
						{
							string change = boost::regex_replace(string(line), boost::regex(".*[$](eventID[[].*[]])[$].*"), string("\\1"));

							if (change != line)
							{
								string oldChange = "$" + change + "$";
								eventIDReplacer(change, "BASE", behaviorFile, eventid, ZeroEvent, l + 1);
								line.replace(line.find(oldChange), oldChange.length(), change);
							}

							if (error) throw nemesis::exception();
						}

						if (line.find("$variableID[", 0) != NOT_FOUND)
						{
							string change = boost::regex_replace(string(line), boost::regex(".*[$](variableID[[].*[]])[$].*"), string("\\1"));

							if (change != line)
							{
								string oldChange = "$" + change + "$";
								variableIDReplacer(change, "BASE", behaviorFile, variableid, ZeroVariable, l + 1);
								line.replace(line.find(oldChange), oldChange.length(), change);
							}

							if (error) throw nemesis::exception();
						}
					}

					if (line.find("$stateID[", 0) != NOT_FOUND)
					{
						string change = boost::regex_replace(string(line), boost::regex(".*[$](stateID[[].*[]][[][0-9]+[]][[].*[]][[][0-9]+[]])[$].*"), string("\\1"));

						if (change != line)
						{
							string oldChange = "$" + change + "$";

							if (registeredStateID[change].length() > 0)
							{
								line.replace(line.find(oldChange), oldChange.length(), registeredStateID[change]);
							}
							else
							{
								string node = boost::regex_replace(string(line), boost::regex(".*[$]stateID[[](.*)[]][[]([0-9]+)[]][[].*[]][[][0-9]+[]][$].*"),
									string("\\1"));
								string base = boost::regex_replace(string(line), boost::regex(".*[$]stateID[[](.*)[]][[]([0-9]+)[]][[].*[]][[][0-9]+[]][$].*"),
									string("\\2"));

								if (stateStrID[node + base] == 0) stateStrID[node + base] = stoi(base) + 1;
								else base = to_string(stateStrID[node + base]++);

								line.replace(line.find(oldChange), oldChange.length(), base);
								registeredStateID[change] = base;
							}
						}
						else
						{
							ErrorMessage(1186, filepath, l + 1, change);
						}
					}
				}

				if (isClipTrigger && line.find("<hkparam name=\"localTime\">-") != NOT_FOUND)
				{
					negative = true;
				}
				else if (negative && line.find("<hkparam name=\"relativeToEndOfClip\">") != NOT_FOUND)
				{
					if (line.find("<hkparam name=\"relativeToEndOfClip\">true</hkparam>") == NOT_FOUND)
					{
						size_t pos = line.find("<hkparam name=\"relativeToEndOfClip\">") + 36;
						line.replace(pos, line.find("</hkparam>", pos) - pos, "true");
					}

					negative = false;
				}

				int repeated = 0;

				while (repeated < repeatnum)
				{
					string newline = line;

					if (newBone && newline.find("%n") != NOT_FOUND)
					{
						while (newline.find("%n") != NOT_FOUND)
						{
							newline.replace(newline.find("%n"), 2, to_string(repeated + oribone));
						}
					}

					catalystMap[curID].push_back(newline);
					++repeated;
				}

				if (elementCatch) elementLine = catalystMap[curID].size() - 1;
			}

			if (error) throw nemesis::exception();
		}

		DebugLogging("Processing behavior: " + filepath + " (Check point 3, Behavior general processing complete)");

		if (clipAA.size() == 0 && pceaID.size() == 0 && !activatedBehavior[lowerBehaviorFile] && !characterAA && !newBone)
		{
			if (!isEdited(BehaviorTemplate.get(), lowerBehaviorFile, newAnimation, isCharacter, modID))
			{
				// final output
#ifdef DEBUG
				outputdir = "new_behaviors\\" + behaviorPath[lowerBehaviorFile].substr(behaviorPath[lowerBehaviorFile].find("\\") + 1);
#else
				outputdir = behaviorPath[lowerBehaviorFile] + ".hkx";
#endif
				if (SSE) lowerBehaviorFile = "SSE\\" + lowerBehaviorFile;

				string cachedFile = "cached_behaviors\\" + lowerBehaviorFile + ".hkx";

				if (isFileExist(cachedFile) && !boost::filesystem::is_directory(cachedFile))
				{
					boost::filesystem::copy_file(cachedFile, outputdir, boost::filesystem::copy_option::overwrite_if_exists);
				}
				else if (isFileExist(outputdir) && !boost::filesystem::is_directory(outputdir))
				{
					if (!boost::filesystem::remove(outputdir))WarningMessage(1005, outputdir);
				}

				int i = 0;

				while (i < 8)
				{
					emit progressAdd();
					++i;
				}

				DebugLogging("Processing behavior: " + filepath + " (Check point 3.4, No changes detected)");
				return;
			}
		}
	}

	if (catalystMap[curID].size() != 0)
	{
		if (catalystMap[curID].back().empty())
		{
			if (catalystMap[curID].size() > 1 && catalystMap[curID][catalystMap[curID].size() - 2].empty()) catalystMap[curID].pop_back();
		}
		else
		{
			catalystMap[curID].push_back("");
		}
	}

	catalystMap[curID].shrink_to_fit();
	emit progressAdd();

	// newAnimation ID in existing function
	if (catcher.size() != 0)
	{
		for (auto it = catcher.begin(); it != catcher.end(); ++it)
		{
			for (unsigned int k = 0; k < it->second.size(); ++k)
			{
				int tempID = it->second[k].getID();
				int tempLine = it->second[k].getLine();
				string* line = &catalystMap[it->second[k].getID()][it->second[k].getLine()];

				if (IDExist[it->first].length() != 0)
				{
					line->append("%");
					string masterFormat = it->first.substr(0, it->first.find("$"));
					string numID = it->first.substr(it->first.find("$") + 1);
					boost::smatch match;
					boost::regex_match(*line, match, boost::regex(".*(" + masterFormat + "[$]" + numID + ")[^0-9]+.*"));
					line->replace(match.position(1), it->first.length(), IDExist[it->first]);
					line->pop_back();
				}
				else
				{
					ErrorMessage(1026, filepath, it->first.substr(0, it->first.find("$")));
				}
			}
		}
	}

	if (error) throw nemesis::exception();

	emit progressAdd();

	// add new animation
	vector<shared_ptr<vecstr>> allEditLines;
	unordered_map<string, bool> isCoreDone;
	unordered_map<int, int> functionState;
	unordered_map<int, shared_ptr<nodeJoint>> existingNodes;
	int grouptimer = 0;
	int onetimer = 0;

	// check for having newAnimation for the file
	if (BehaviorTemplate->grouplist.find(lowerBehaviorFile) != BehaviorTemplate->grouplist.end() && BehaviorTemplate->grouplist[lowerBehaviorFile].size() > 0)
	{
		for (auto& templateCode : BehaviorTemplate->grouplist[lowerBehaviorFile])
		{
			vecstr opening;
			opening.push_back("<!-- ======================== NEMESIS " + templateCode + " TEMPLATE START ======================== -->");
			allEditLines.emplace_back(make_shared<vecstr>(opening));
			bool hasGroup = false;
			bool hasMaster = false;
			bool ignoreGroup = false;

			if (BehaviorTemplate->behaviortemplate.find(templateCode + "_group") != BehaviorTemplate->behaviortemplate.end())
			{
				if (BehaviorTemplate->behaviortemplate[templateCode + "_group"].find(lowerBehaviorFile) != BehaviorTemplate->behaviortemplate[templateCode +
					"_group"].end())
				{
					if (BehaviorTemplate->behaviortemplate[templateCode + "_group"][lowerBehaviorFile].size() > 0) hasGroup = true;
				}
			}

			if (BehaviorTemplate->behaviortemplate.find(templateCode + "_master") != BehaviorTemplate->behaviortemplate.end())
			{
				if (BehaviorTemplate->behaviortemplate[templateCode + "_master"].find(lowerBehaviorFile) != BehaviorTemplate->behaviortemplate[templateCode +
					"_master"].end())
				{
					if (BehaviorTemplate->behaviortemplate[templateCode + "_master"][lowerBehaviorFile].size() > 0) hasMaster = true;
				}
			}

			if (newAnimation.find(templateCode) != newAnimation.end() && newAnimation[templateCode].size() != 0)
			{
				shared_ptr<master> groupFunctionIDs = make_shared<master>();
				vector<vector<shared_ptr<animationInfo>>> groupAnimInfo;

				{
					vecstr space{ "" };
					allEditLines.emplace_back(make_shared<vecstr>(space));
				}

				if (newAnimation[templateCode].size() != 0 && !newAnimSkip(newAnimation[templateCode], modID))
				{
					shared_ptr<group> subFunctionIDs = make_shared<group>();
					subFunctionIDs->singlelist.reserve(memory);
					group newSubFunctionIDs;
					vector<int> stateID;
					vector<int> stateMultiplier = { 0 };
					GetStateCount(stateMultiplier, BehaviorTemplate->behaviortemplate[templateCode][lowerBehaviorFile], templateCode, behaviorFile, hasGroup);

					if (!hasGroup)
					{
						stateID = GetStateID(BehaviorTemplate->mainBehaviorJoint[templateCode][lowerBehaviorFile], catalystMap, functionState);

						if (newAnimation[templateCode][0]->GetGroupAnimInfo()[0]->ignoreGroup)
						{
							ignoreGroup = true;
							// newSubFunctionIDs.singlelist.reserve(memory);
							groupFunctionIDs->grouplist.reserve(memory);
						}
					}
					else
					{
						stateID.push_back(0);
						groupFunctionIDs->grouplist.reserve(memory);
					}

					if (error) throw nemesis::exception();

					if (stateMultiplier.size() != stateID.size() || stateMultiplier.size() == 0) ErrorMessage(1192);

					size_t newAnimCount = newAnimation[templateCode].size();

					if (newAnimCount > 0)
					{
						DebugLogging("Processing behavior: " + filepath + " (Check point 3.6, Mod code: " + templateCode + ", Animation count: " +
							to_string(newAnimCount) + ")");
						shared_ptr<Furniture> dummyAnimation;
						int IDMultiplier = newAnimation[templateCode][0]->getNextID(lowerBehaviorFile);
						newAnimLock animLock;
						size_t n_core = boost::thread::hardware_concurrency();
						boost::posix_time::ptime start_time = boost::posix_time::microsec_clock::local_time();

						if (newAnimCount > 500) n_core = n_core * 3 / 4;

						// individual animation
						if (hasGroup)
						{
							for (unsigned int k = 0; k < newAnimCount; ++k)
							{
								vector<unique_ptr<std::thread>> multiThreads;

								try
								{
									for (k; k < newAnimCount; ++k)
									{
										if (newAnimation[templateCode][k]->coreModID != modID)
										{
											if (dummyAnimation != nullptr && !dummyAnimation->isLast()) ErrorMessage(1183);
										}
										else
										{
											subFunctionIDs->singlelist.emplace_back(make_shared<single>());
											subFunctionIDs->singlelist.back()->format["Nemesis" + modID + lowerBehaviorFile + to_string(k)] = to_string(k);
											allEditLines.emplace_back(make_shared<vecstr>());
											dummyAnimation = newAnimation[templateCode][k];

											shared_ptr<newAnimArgs> args = make_shared<newAnimArgs>(modID, lowerBehaviorFile, lastID, BehaviorTemplate->optionlist[templateCode].core,
												newAnimation[templateCode][k], allEditLines.back(), isCoreDone[newAnimation[templateCode][k]->GetGroupAnimInfo()[0]->filename],
												functionState, exportID, eventid, variableid, animLock, ZeroEvent, ZeroVariable, hasGroup, stateID, stateMultiplier,
												subFunctionIDs->singlelist.back(), subFunctionIDs, groupFunctionIDs, false, groupAnimInfo);

											if (newAnimation[templateCode][k]->isLast())
											{
												subFunctionIDs->singlelist.shrink_to_fit();
												groupFunctionIDs->grouplist.push_back(subFunctionIDs);
												subFunctionIDs = make_shared<group>();
												subFunctionIDs->singlelist.reserve(memory);
												groupAnimInfo.push_back(newAnimation[templateCode][k]->GetGroupAnimInfo());

												for (unsigned int statenum = 0; statenum < stateID.size(); ++statenum)
												{
													stateID[statenum] = 0;
												}
											}
											else
											{
												for (unsigned int statenum = 0; statenum < stateID.size(); ++statenum)
												{
													stateID[statenum] += stateMultiplier[statenum];
												}
											}

											if (error) throw nemesis::exception();

											lastID += IDMultiplier;
											multiThreads.emplace_back(make_unique<std::thread>(animThreadStart, args));
										}

										if (multiThreads.size() + m_RunningThread >= n_core + extraCore) break;
									}
								}
								catch (nemesis::exception&)
								{
									// resolved exception
								}

								if (multiThreads.size() > 0)
								{
									for (unique_ptr<std::thread>& thrd : multiThreads)
									{
										thrd->join();
									}
								}
							}
						}
						else
						{
							for (unsigned int k = 0; k < newAnimCount; ++k)
							{
								vector<unique_ptr<std::thread>> multiThreads;

								try
								{
									for (k; k < newAnimCount; ++k)
									{
										if (newAnimation[templateCode][k]->coreModID != modID)
										{
											if (dummyAnimation != nullptr && !dummyAnimation->isLast()) ErrorMessage(1183);
										}
										else
										{
											subFunctionIDs->singlelist.emplace_back(make_shared<single>());
											subFunctionIDs->singlelist.back()->format["Nemesis" + modID + lowerBehaviorFile + to_string(k)] = to_string(k);
											allEditLines.emplace_back(make_shared<vecstr>());
											dummyAnimation = newAnimation[templateCode][k];

											shared_ptr<newAnimArgs> args = make_shared<newAnimArgs>(modID, lowerBehaviorFile, lastID, BehaviorTemplate->optionlist[templateCode].core,
												newAnimation[templateCode][k], allEditLines.back(), isCoreDone[newAnimation[templateCode][k]->GetGroupAnimInfo()[0]->filename],
												functionState, exportID, eventid, variableid, animLock, ZeroEvent, ZeroVariable, hasGroup, stateID, stateMultiplier,
												subFunctionIDs->singlelist.back(), subFunctionIDs, groupFunctionIDs, ignoreGroup, groupAnimInfo);

											if (ignoreGroup)
											{
												if (newAnimation[templateCode][k]->isLast())
												{
													subFunctionIDs->singlelist.shrink_to_fit();
													groupFunctionIDs->grouplist.push_back(subFunctionIDs);
													subFunctionIDs = make_shared<group>();
													subFunctionIDs->singlelist.reserve(memory);
													groupAnimInfo.push_back(newAnimation[templateCode][k]->GetGroupAnimInfo());

													for (unsigned int statenum = 0; statenum < stateID.size(); ++statenum)
													{
														stateID[statenum] += stateMultiplier[statenum];
													}
												}
											}
											else
											{
												groupAnimInfo.push_back(newAnimation[templateCode][k]->GetGroupAnimInfo());

												for (unsigned int statenum = 0; statenum < stateID.size(); ++statenum)
												{
													stateID[statenum] += stateMultiplier[statenum];
												}
											}

											if (error) throw nemesis::exception();

											lastID += IDMultiplier;
											multiThreads.emplace_back(make_unique<std::thread>(animThreadStart, args));
										}

										if (multiThreads.size() + m_RunningThread >= n_core + extraCore) break;
									}
								}
								catch (nemesis::exception&)
								{
									// resolved exception
								}

								if (multiThreads.size() > 0)
								{
									for (unique_ptr<std::thread>& thrd : multiThreads)
									{
										thrd->join();
									}
								}
							}
						}

						if (!ignoreGroup && !hasGroup)
						{
							subFunctionIDs->singlelist.shrink_to_fit();
							groupFunctionIDs->grouplist.push_back(subFunctionIDs);
							subFunctionIDs = make_shared<group>();
							vector<shared_ptr<animationInfo>> subGroupAnimInfo;
							vector<vector<shared_ptr<animationInfo>>> newGroupAnimInfo;

							for (unsigned int l = 0; l < groupAnimInfo.size(); ++l)
							{
								subGroupAnimInfo.push_back(groupAnimInfo[l][0]);
							}

							newGroupAnimInfo.push_back(subGroupAnimInfo);
							groupAnimInfo = newGroupAnimInfo;
						}

						// check error before initialization
						for (unsigned int curGroup = 0; curGroup < groupFunctionIDs->grouplist.size(); ++curGroup)
						{
							if (groupFunctionIDs->grouplist[curGroup]->singlelist.size() != groupAnimInfo[curGroup].size())ErrorMessage(1156, templateCode);
						}

						boost::posix_time::time_duration diff = boost::posix_time::microsec_clock::local_time() - start_time;
						onetimer += int(diff.total_milliseconds());
						start_time = boost::posix_time::microsec_clock::local_time();

						// group animation
						if (hasGroup && groupFunctionIDs->grouplist.size() > 0)
						{
							string filename = templateCode + "_group";
							shared_ptr<AnimTemplate> grouptemplate_pack = make_shared<AnimTemplate>();
							grouptemplate_pack->ExamineTemplate(templateCode, lowerBehaviorFile, BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile], true, false,
								BehaviorTemplate->optionlist[templateCode]);
							IDMultiplier = getTemplateNextID(BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile]);
							stateMultiplier = vector<int>();

							if (!GetStateCount(stateMultiplier, BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile], templateCode, behaviorFile, hasGroup)) return;

							hasMaster ? stateID = { 0 } : stateID = GetStateID(BehaviorTemplate->mainBehaviorJoint[templateCode][lowerBehaviorFile], catalystMap, functionState);
							size_t n_newAnimCount = groupFunctionIDs->grouplist.size();

							for (unsigned int k = 0; k < n_newAnimCount; ++k)
							{
								vector<unique_ptr<std::thread>> multiThreads;

								try
								{
									for (k; k < n_newAnimCount; ++k)
									{
										allEditLines.emplace_back(make_shared<vecstr>());
										shared_ptr<groupTemplate> groupTemp = make_shared<groupTemplate>(BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile],
											grouptemplate_pack);
										groupTemp->setZeroEvent(ZeroEvent);
										groupTemp->setZeroVariable(ZeroVariable);
										shared_ptr<newGroupArgs> args = make_shared<newGroupArgs>(templateCode, lowerBehaviorFile, filename, lastID, k + 1, stateID, groupTemp,
											allEditLines.back(), exportID, eventid, variableid, animLock, ZeroEvent, ZeroVariable, groupFunctionIDs, groupAnimInfo);

										for (unsigned int statenum = 0; statenum < stateID.size(); ++statenum)
										{
											stateID[statenum] += stateMultiplier[statenum];
										}

										if (error) throw nemesis::exception();

										lastID += IDMultiplier;
										multiThreads.emplace_back(make_unique<std::thread>(groupThreadStart, args));

										if (multiThreads.size() + m_RunningThread >= n_core + extraCore) break;
									}
								}
								catch (nemesis::exception&)
								{
									// resolved exception
								}

								if (multiThreads.size() > 0)
								{
									for (unique_ptr<std::thread>& thrd : multiThreads)
									{
										thrd->join();
									}
								}
							}
						}

						// master animation
						if (hasMaster)
						{
							string filename = templateCode + "_master";
							shared_ptr<AnimTemplate> mastertemplate_pack = make_shared<AnimTemplate>();
							mastertemplate_pack->ExamineTemplate(templateCode, lowerBehaviorFile, BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile], true, true,
								BehaviorTemplate->optionlist[templateCode]);
							stateID = GetStateID(BehaviorTemplate->mainBehaviorJoint[templateCode][lowerBehaviorFile], catalystMap, functionState);
							shared_ptr<groupTemplate> masterTemp = make_shared<groupTemplate>(BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile],
								mastertemplate_pack);
							masterTemp->setZeroEvent(ZeroEvent);
							masterTemp->setZeroVariable(ZeroVariable);
							allEditLines.emplace_back(make_shared<vecstr>());
							masterTemp->getFunctionLines(allEditLines.back(), lowerBehaviorFile, filename, stateID, groupFunctionIDs,
								groupAnimInfo, lastID, exportID, eventid, variableid, templateCode, animLock, -1);

							lastID += getTemplateNextID(BehaviorTemplate->behaviortemplate[filename][lowerBehaviorFile]);

							if (error) throw nemesis::exception();
						}

						diff = boost::posix_time::microsec_clock::local_time() - start_time;
						grouptimer += int(diff.total_milliseconds());
						DebugLogging("Processing behavior: " + filepath + " (Check point 3.8, Mod code: " + templateCode + ", Animation count: " +
							to_string(newAnimCount) + " COMPLETE)");
					}
				}

				DebugLogging("Processing behavior: " + filepath + " (Check point 3.8, Mod code: " + templateCode + ", Existing ID count: " +
					to_string(BehaviorTemplate->existingFunctionID[templateCode][lowerBehaviorFile].size()) + ")");

				processExistFuncID(BehaviorTemplate->existingFunctionID[templateCode][lowerBehaviorFile], ZeroEvent, ZeroVariable, catalystMap, groupFunctionIDs,
					groupAnimInfo, templateCode, exportID, eventid, variableid, lastID, hasMaster, hasGroup, BehaviorTemplate->grouplist[lowerBehaviorFile], ignoreGroup,
					existingNodes);

				DebugLogging("Processing behavior: " + filepath + " (Check point 3.8, Mod code: " + templateCode + ", Existing ID count: " +
					to_string(BehaviorTemplate->existingFunctionID[templateCode][lowerBehaviorFile].size()) + " COMPLETE)");
			}
			else
			{
				DebugLogging("Processing behavior: " + filepath + " (Check point 3.8, Mod code: " + templateCode + ", Existing ID count: " +
					to_string(BehaviorTemplate->existingFunctionID[templateCode][lowerBehaviorFile].size()) + ")");

				processExistFuncID(BehaviorTemplate->existingFunctionID[templateCode][lowerBehaviorFile], ZeroEvent, ZeroVariable, catalystMap, make_shared<master>(),
					vector<vector<shared_ptr<animationInfo>>>(), templateCode, exportID, eventid, variableid, lastID, hasMaster, hasGroup,
					BehaviorTemplate->grouplist[lowerBehaviorFile], ignoreGroup, existingNodes);

				DebugLogging("Processing behavior: " + filepath + " (Check point 3.8, Mod code: " + templateCode + ", Existing ID count: " +
					to_string(BehaviorTemplate->existingFunctionID[templateCode][lowerBehaviorFile].size()) + " COMPLETE)");
			}

			vecstr closing;
			closing.push_back("<!-- ======================== NEMESIS " + templateCode + " TEMPLATE END ======================== -->");
			closing.push_back("");
			allEditLines.emplace_back(make_unique<vecstr>(closing));
		}
	}

	// check for error
	if (error) throw nemesis::exception();

	// load to existing Nodes from behavior template
	unpackToCatalyst(catalystMap, existingNodes);

	DebugLogging("Total single animation processing time for " + behaviorFile + ": " + to_string(onetimer));
	DebugLogging("Total group animation processing time for " + behaviorFile + ": " + to_string(grouptimer));
	DebugLogging("Processing behavior: " + filepath + " (Check point 4, New animation inclusion complete)");
	emit progressAdd();

	// AA animation installation
	vecstr AAlines;

	if (clipAA.size() != 0)
	{
		DebugLogging("Processing behavior: " + filepath + " (Check point 4.2, AA count: " + to_string(clipAA.size()) + ")");
		unordered_map<string, int> replacerCount;

		for (auto iter = clipAA.begin(); iter != clipAA.end(); ++iter)
		{
			bool isChange = false;
			vecstr msglines;

			if (error) throw nemesis::exception();

			for (auto it = iter->second.begin(); it != iter->second.end(); ++it)
			{
				for (auto& animName : it->second)
				{
					if (animName != "x")
					{
						isChange = true;
						break;
					}
				}

				if (!isChange) break;

				vecstr children;
				string groupName = AAGroup[it->first];
				string importline = to_string(iter->first);

				while (importline.length() < 4)
				{
					importline = "0" + importline;
				}

				msglines.push_back("		<hkobject name=\"#" + importline + "\" class=\"hkbManualSelectorGenerator\" signature=\"0xd932fab8\">");
				importline = "variableID[Nemesis_AA" + (isFirstPerson ? groupName + "_1p*" : groupName) + "]";
				variableIDReplacer(importline, "AA", behaviorFile, variableid, ZeroVariable, 0);

				if (error) throw nemesis::exception();

				if (exportID["variable_binding"]["selectedGeneratorIndex!~^!" + importline].length() == 0)
				{
					string tempID = to_string(lastID);

					while (tempID.length() < 4)
					{
						tempID = "0" + tempID;
					}

					exportID["variable_binding"]["selectedGeneratorIndex!~^!" + importline] = tempID;
					importline = "			<hkparam name=\"variableBindingSet\">#" + tempID + "</hkparam>";
					++lastID;

					if (lastID == 9216) ++lastID;
				}
				else
				{
					importline = "			<hkparam name=\"variableBindingSet\">#" + exportID["variable_binding"]
						["selectedGeneratorIndex!~^!" + importline] + "</hkparam>";
				}

				msglines.push_back(importline);
				msglines.push_back("			<hkparam name=\"userData\">0</hkparam>");
				replacerCount[it->first]++;
				msglines.push_back("			<hkparam name=\"name\">Nemesis_" + to_string(replacerCount[it->first]) + "_" + groupName + "_MSG</hkparam>");
				msglines.push_back("			<hkparam name=\"generators\" numelements=\"" + to_string(it->second.size() + 1) + "\">");

				string baseID = to_string(lastID);

				while (baseID.length() < 4)
				{
					baseID = "0" + baseID;
				}

				msglines.push_back("				#" + baseID);
				++lastID;

				if (lastID == 9216) ++lastID;

				for (unsigned int i = 0; i < it->second.size(); ++i)
				{
					if (it->second[i] != "x")
					{
						string tempID = to_string(lastID);

						while (tempID.length() < 4)
						{
							tempID = "0" + tempID;
						}

						msglines.push_back("				#" + tempID);
						children.push_back(tempID);
						++lastID;

						if (lastID == 9216) ++lastID;
					}
					else
					{
						msglines.push_back("				#" + baseID);
					}
				}

				msglines.push_back("			</hkparam>");
				msglines.push_back("			<hkparam name=\"selectedGeneratorIndex\">0</hkparam>");
				msglines.push_back("			<hkparam name=\"currentGeneratorIndex\">0</hkparam>");
				msglines.push_back("		</hkobject>");
				msglines.push_back("");

				unordered_map<string, vecstr> triggerID;

				{
					int i_baseID = stoi(baseID);
					auto& pceaBaseIter = pceaID.find(iter->first);
					catalystMap[i_baseID].reserve(catalystMap[iter->first].size());
					catalystMap[i_baseID].push_back("		<hkobject name=\"#" + baseID + "\" class=\"hkbClipGenerator\" signature=\"0x333b85b9\">");

					for (unsigned int i = 1; i < catalystMap[iter->first].size(); ++i)
					{
						catalystMap[i_baseID].push_back(catalystMap[iter->first][i]);
					}

					if (catalystMap[i_baseID].back().length() != 0) catalystMap[i_baseID].push_back("");

					if (pceaBaseIter != pceaID.end())
					{
						pceaID[i_baseID] = pceaBaseIter->second;
						pceaID.erase(pceaBaseIter);
					}
				}

				int num = 0;
				AAlines.reserve(catalystMap[iter->first].size() * children.size());

				for (unsigned int i = 0; i < children.size(); ++i)
				{
					AAlines.push_back("		<hkobject name=\"#" + children[i] + "\" class=\"hkbClipGenerator\" signature=\"0x333b85b9\">");
					AAlines.push_back(catalystMap[iter->first][1]);
					AAlines.push_back(catalystMap[iter->first][2]);
					AAlines.push_back(catalystMap[iter->first][3]);

					while (it->second[num] == "x")
					{
						++num;
					}

					AAlines.push_back("			<hkparam name=\"animationName\">Animations\\" + it->second[num] + "</hkparam>");
					string animFile = boost::to_lower_copy(it->second[num].substr(it->second[num].find_last_of("\\") + 1));
					auto& aaEvent_itr = AAEvent.find(isFirstPerson ? animFile + "_1p*" : animFile);

					if (aaEvent_itr != AAEvent.end())
					{
						if (aaEvent_itr->second.size() > 0)
						{
							string tempID = to_string(lastID);

							while (tempID.length() < 4)
							{
								tempID = "0" + tempID;
							}

							AAlines.push_back("			<hkparam name=\"triggers\">#" + tempID + "</hkparam>");
							triggerID[tempID] = aaEvent_itr->second;
							++lastID;

							if (lastID == 9216) ++lastID;
						}
						else
						{
							AAlines.push_back("			<hkparam name=\"triggers\">null</hkparam>");
						}
					}
					else
					{
						AAlines.push_back(catalystMap[iter->first][5]);
					}

					for (unsigned int j = 6; j < catalystMap[iter->first].size(); ++j)
					{
						AAlines.push_back(catalystMap[iter->first][j]);
					}

					if (AAlines.back().length() != 0) AAlines.push_back("");

					if (error) throw nemesis::exception();

					++num;
				}

				for (auto itera = triggerID.begin(); itera != triggerID.end(); ++itera)
				{
					AAlines.reserve(AAlines.size() + (itera->second.size() * 12) + 5);
					AAlines.push_back("		<hkobject name=\"#" + itera->first + "\" class=\"hkbClipTriggerArray\" signature=\"0x59c23a0f\">");
					AAlines.push_back("			<hkparam name=\"triggers\" numelements=\"" + to_string(itera->second.size() / 2) + "\">");

					for (unsigned int i = 0; i < itera->second.size(); ++i)
					{
						bool negative = false;
						string time = itera->second[i + 1];

						if (time[0] == '-')
						{
							negative = true;
							string signTime = time.substr(1);

							if (isOnlyNumber(signTime) && stod(signTime) == 0) time = signTime;
						}

						AAlines.push_back("				<hkobject>");
						AAlines.push_back("					<hkparam name=\"localTime\">" + time + "</hkparam>");
						AAlines.push_back("					<hkparam name=\"event\">");
						AAlines.push_back("						<hkobject>");

						string eventID = "eventID[" + itera->second[i] + "]";
						eventIDReplacer(eventID, "AA", behaviorFile, eventid, ZeroEvent, 0);

						if (error) throw nemesis::exception();

						AAlines.push_back("							<hkparam name=\"id\">" + eventID + "</hkparam>");
						AAlines.push_back("							<hkparam name=\"payload\">null</hkparam>");
						AAlines.push_back("						</hkobject>");
						AAlines.push_back("					</hkparam>");

						AAlines.push_back("					<hkparam name=\"relativeToEndOfClip\">" + string(negative ? "true" : "false") + "</hkparam>");

						AAlines.push_back("					<hkparam name=\"acyclic\">false</hkparam>");
						AAlines.push_back("					<hkparam name=\"isAnnotation\">false</hkparam>");
						AAlines.push_back("				</hkobject>");

						++i;
					}

					if (error) throw nemesis::exception();

					AAlines.push_back("			</hkparam>");
					AAlines.push_back("		</hkobject>");
					AAlines.push_back("");
				}

				if (error) throw nemesis::exception();
			}

			if (isChange) catalystMap[iter->first] = msglines;
		}

		DebugLogging("Processing behavior: " + filepath + " (Check point 4.4, AA count: " + to_string(clipAA.size()) + " COMPLETE)");
	}

	vecstr PCEALines;

	if (pceaID.size() > 0)
	{
		DebugLogging("Processing behavior: " + filepath + " (Check point 4.6, PCEA count: " + to_string(pceaID.size()) + ")");
		unordered_map<string, int> replacerCount;

		for (auto& datalist : pceaID)
		{
			vector<vecstr> lineRe;
			string importline = to_string(datalist.first);

			if (error) throw nemesis::exception();

			for (auto& data = datalist.second->rbegin(); data != datalist.second->rend(); ++data)
			{
				while (importline.length() < 4)
				{
					importline = "0" + importline;
				}

				// populating manual selector generator
				vecstr msglines;
				msglines.push_back("		<hkobject name=\"#" + importline + "\" class=\"hkbManualSelectorGenerator\" signature=\"0xd932fab8\">");
				importline = "variableID[" + data->modFile + "]";
				variableIDReplacer(importline, "PCEA", behaviorFile, variableid, ZeroVariable, 0);

				if (exportID["variable_binding"]["selectedGeneratorIndex!~^!" + importline].length() == 0)
				{
					string tempID = to_string(lastID);

					while (tempID.length() < 4)
					{
						tempID = "0" + tempID;
					}

					exportID["variable_binding"]["selectedGeneratorIndex!~^!" + importline] = tempID;
					importline = "			<hkparam name=\"variableBindingSet\">#" + tempID + "</hkparam>";
					++lastID;

					if (lastID == 9216) ++lastID;
				}
				else
				{
					importline = "			<hkparam name=\"variableBindingSet\">#" + exportID["variable_binding"]
						["selectedGeneratorIndex!~^!" + importline] + "</hkparam>";
				}

				msglines.push_back(importline);
				msglines.push_back("			<hkparam name=\"userData\">0</hkparam>");
				++replacerCount[data->modFile];
				msglines.push_back("			<hkparam name=\"name\">Nemesis_PCEA_" + data->modFile + "_MSG</hkparam>");
				msglines.push_back("			<hkparam name=\"generators\" numelements=\"2\">");

				importline = to_string(lastID);

				while (importline.length() < 4)
				{
					importline = "0" + importline;
				}

				msglines.push_back("				#" + importline);
				++lastID;

				if (lastID == 9216) ++lastID;

				string baseID = to_string(lastID);

				while (baseID.length() < 4)
				{
					baseID = "0" + baseID;
				}

				msglines.push_back("				#" + baseID);
				++lastID;

				if (lastID == 9216) ++lastID;

				msglines.push_back("			</hkparam>");
				msglines.push_back("			<hkparam name=\"selectedGeneratorIndex\">0</hkparam>");
				msglines.push_back("			<hkparam name=\"currentGeneratorIndex\">0</hkparam>");
				msglines.push_back("		</hkobject>");
				msglines.push_back("");

				lineRe.push_back(msglines);
				msglines.clear();

				// populating clip generator
				msglines.push_back("		<hkobject name=\"#" + baseID + "\" class=\"hkbClipGenerator\" signature=\"0x333b85b9\">");
				msglines.push_back(catalystMap[datalist.first][1]);
				msglines.push_back(catalystMap[datalist.first][2]);
				msglines.push_back(catalystMap[datalist.first][3]);
				msglines.push_back("			<hkparam name=\"animationName\">" + data->animPath + "</hkparam>");

				for (unsigned int j = 5; j < catalystMap[datalist.first].size(); ++j)
				{
					msglines.push_back(catalystMap[datalist.first][j]);
				}

				if (msglines.back().length() != 0) msglines.push_back("");

				if (error) throw nemesis::exception();

				lineRe.push_back(msglines);
			}

			vecstr msglines;
			msglines.push_back("		<hkobject name=\"#" + importline + "\" class=\"hkbClipGenerator\" signature=\"0x333b85b9\">");
			msglines.insert(msglines.end(), catalystMap[datalist.first].begin() + 1, catalystMap[datalist.first].end());

			if (msglines.back().length() != 0) msglines.push_back("");

			lineRe.push_back(msglines);

			for (auto& it = lineRe.rbegin(); it != lineRe.rend(); ++it)
			{
				PCEALines.insert(PCEALines.end(), it->begin(), it->end());
			}

			if (error) throw nemesis::exception();

			catalystMap.erase(catalystMap.find(datalist.first));
		}

		DebugLogging("Processing behavior: " + filepath + " (Check point 4.8, PCEA count: " + to_string(pceaID.size()) + " COMPLETE)");
	}

	emit progressAdd();

	size_t reserveSize = 0;
	vecstr behaviorlines;

	// output import functions
	// Must be in vector
	vector<ImportContainer> groupExportID;
	groupExportID.push_back(exportID);
	vecstr additionallines = importOutput(groupExportID, 0, lastID, filelist[curList]);

	emit progressAdd();

	DebugLogging("Processing behavior: " + filepath + " (Check point 5, Prepare to output)");
	emit progressAdd();

	if (behaviorPath[lowerBehaviorFile].size() == 0) ErrorMessage(1068, behaviorFile);

	string filename = "temp_behaviors\\xml\\" + modID + lowerBehaviorFile;

	if (!FolderCreate(GetFileDirectory(filename)) || !FolderCreate(GetFileDirectory(outputdir))) return;

	ofstream output(filename + ".xml");

	if (!output.is_open()) ErrorMessage(1025, filename);

	for (int i = firstID; i < firstID + 4; ++i)
	{
		for (auto& eachline : catalystMap[i])
		{
			output << eachline + "\n";
		}

		if (error) throw nemesis::exception();
	}

	output << "<!-- ======================== NEMESIS import TEMPLATE START ======================== -->\n";

	if (additionallines.size() > 0)
	{
		output << "\n";

		for (auto& eachline : additionallines)
		{
			output << eachline + "\n";
		}

		if (error) throw nemesis::exception();
	}

	output << "<!-- ======================== NEMESIS import TEMPLATE END ======================== -->\n\n";
	output << "<!-- ======================== NEMESIS PCEA TEMPLATE START ======================== -->\n";

	if (PCEALines.size() > 0)
	{
		output << "\n";

		for (auto& eachline : PCEALines)
		{
			output << eachline + "\n";
		}

		if (error) throw nemesis::exception();
	}

	output << "<!-- ======================== NEMESIS PCEA TEMPLATE END ======================== -->\n\n";
	output << "<!-- ======================== NEMESIS alternate animation TEMPLATE START ======================== -->\n";

	if (AAlines.size() > 0)
	{
		output << "\n";

		for (auto& eachline : AAlines)
		{
			output << eachline + "\n";
		}

		if (error) throw nemesis::exception();
	}

	output << "<!-- ======================== NEMESIS alternate animation TEMPLATE END ======================== -->\n\n";

	for (unsigned int j = 0; j < allEditLines.size(); ++j)
	{
		for (auto& eachline : (*allEditLines[j]))
		{
			output << eachline + "\n";
		}

		if (error) throw nemesis::exception();
	}

	firstID = firstID + 4;
	size_t size = catalystMap.rbegin()->first + 1;

	for (size_t it = firstID; it < size; ++it)
	{
		for (auto& eachline : catalystMap[it])
		{
			output << eachline + "\n";
		}

		if (error) throw nemesis::exception();
	}

	if (catalystMap.rbegin()->second.back().length() != 0) output << "\n";

	output << "	</hksection>\n\n";
	output << "</hkpackfile>\n";
	output.close();

	if (error) throw nemesis::exception();

	DebugLogging("Processing behavior: " + filepath + " (Check point 6, Behavior output complete)");
	emit progressAdd();
	++extraCore;

	if (hkxcmdProcess(filename, outputdir))
	{
		DebugLogging("Processing behavior: " + filepath + " (Check point 7, Behavior compile complete)");
		emit progressAdd();
	}

	--extraCore;
}

void BehaviorSub::addInfo(string& newDirectory, vecstr& newfilelist, int newCurList, vecstr& newBehaviorPriority, unordered_map<string,
	bool>& newChosenBehavior, shared_ptr<getTemplate> newBehaviorTemplate, unordered_map<string, vector<shared_ptr<Furniture>>>& addAnimation,
	unordered_map<string, var>& newAnimVar, mapSetString& addAnimEvent, mapSetString& addAnimVariable, unordered_map<string, unordered_map<int,
	bool>>&newIgnoreFunction, bool newIsCharacter, string newModID)
{
	directory = newDirectory;
	filelist = newfilelist;
	curList = newCurList;
	behaviorPriority = newBehaviorPriority;
	chosenBehavior = newChosenBehavior;
	BehaviorTemplate = newBehaviorTemplate;
	newAnimation = addAnimation;
	AnimVar = newAnimVar;
	newAnimEvent = addAnimEvent;
	newAnimVariable = addAnimVariable;
	ignoreFunction = newIgnoreFunction;
	isCharacter = newIsCharacter;
	modID = newModID;
}

void BehaviorSub::addAnimation()
{
	++animCounter;

	if (animCounter > base) emit newAnim();
}


