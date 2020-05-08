#pragma once
#include <EuroScopePlugIn.h>

#include <vector>
#include <map>
#include <algorithm>
#include <time.h>
#include <GdiPlus.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <thread>
#include <memory>

#include <asio/io_service.hpp>
#include "bstrlib/bstrwrap.h"

#include "Constant.hpp"
#include "CallsignLookup.hpp"
#include "Config.hpp"
#include "Rimcas.hpp"
#include "InsetWindow.h"
#include "ColorManager.h"
#include "Logger.h"

using namespace std;
using namespace Gdiplus;
using namespace EuroScopePlugIn;


//namespace SMRSharedData {
	//static vector<string> ReleasedTracks;	
//};

#define PATATOIDE_NUM_OUTER_POINTS 12
#define PATATOIDE_NUM_INNER_POINTS 7
#define ACT_TYPE_EMPTY_SPACES "      "
#define GATE_EMPTY_SPACES "           "
#define SCRATCHPAD_EMPTY_SPACES "                          "

namespace SMRPluginSharedData {
	static asio::io_service io_service;
}

//using namespace SMRSharedData;

class CSMRRadar :
	public EuroScopePlugIn::CRadarScreen
{
public:
	CSMRRadar();
	virtual ~CSMRRadar();

	static set<CBString> manuallyCorrelated;
	static map<CBString, CBString> vStripsStands;

	bool BLINK = false;

	map<CBString, POINT> TagsOffsets;

	//vector<string> Active_Arrivals;

	clock_t clock_init, clock_final;

	COLORREF SMR_TARGET_COLOR = RGB(255, 242, 73);
	COLORREF SMR_H1_COLOR = RGB(0, 255, 255);
	COLORREF SMR_H2_COLOR = RGB(0, 219, 219);
	COLORREF SMR_H3_COLOR = RGB(0, 183, 183);

	struct TagItem
	{
		CBString value;
		int function;
	};


	struct Patatoide_Points
	{
		CPosition points[PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS];
		CPosition history_one_points[PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS];
		CPosition history_two_points[PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS];
		CPosition history_three_points[PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS];
	};

	/*
	struct Patatoide_Points
	{
		map<int, POINT2> points;
		map<int, POINT2> History_one_points;
		map<int, POINT2> History_two_points;
		map<int, POINT2> History_three_points;
	};
	*/

	map<CBString, Patatoide_Points> Patatoides;

	map<string, bool> ClosedRunway;

	CBString ConfigPath;
	CCallsignLookup * Callsigns;
	CColorManager * ColorManager;

	map<CBString, bool> ShowLists;
	map<CBString, RECT> ListAreas;

	map<int, bool> appWindowDisplays;

	set<CBString> tagDetailed;
	map<CBString, CRect> tagAreas;
	map<CBString, double> TagAngles;
	map<CBString, int> TagLeaderLineLength;

	bool QDMenabled = false;
	bool QDMSelectEnabled = false;
	POINT QDMSelectPt;
	POINT QDMmousePt;

	bool ColorSettingsDay = true;
	bool isLVP = false;

	map<CBString, RECT> TimePopupAreas;

	map<int, CBString> TimePopupData;
	//multimap<string, string> AcOnRunway;
	map<CBString, bool> ColorAC;

	//map<string, CRimcas::RunwayAreaType> RunwayAreas;

	map<CBString, RECT> MenuPositions;
	map<CBString, bool> DisplayMenu;

	map<CBString, clock_t> RecentlyAutoMovedTags;

	CRimcas * RimcasInstance = nullptr;
	CConfig * CurrentConfig = nullptr;

	Gdiplus::Font* customFont;
	int currentFontSize = 15;

	map<CBString, CPosition> AirportPositions;

	bool isProMode = false;
	bool useAutoDeconfliction = false;
	bool Afterglow = true;

	int Trail_Gnd = 4;
	int Trail_App = 4;
	int PredictedLenght = 0;

	bool NeedCorrelateCursor = false;
	bool ReleaseInProgress = false;
	bool AcquireInProgress = false;

	multimap<CBString, CBString> DistanceTools;
	bool DistanceToolActive = false;
	pair<CBString, CBString> ActiveDistance;

	//----
	// Tag types
	//---

	enum TagTypes { Departure, Arrival, Airborne, Uncorrelated };

	CBString ActiveAirport = "LSZH";


	//---GenerateTagData--------------------------------------------

	static map<CBString, TagItem> GenerateTagData(CRadarTarget rt, CFlightPlan fp, CSMRRadar* radar, CBString ActiveAirport);

	//---IsCorrelatedFuncs---------------------------------------------

	bool IsCorrelated(CFlightPlan fp, CRadarTarget rt)
	{		
		if (isProMode) {
			if (fp.IsValid() && fp.GetFlightPlanData().IsReceived()) {				
				if (strcmp(fp.GetControllerAssignedData().GetSquawk(), rt.GetPosition().GetSquawk()) == 0) {
					return true;
				}

				/*if (CurrentConfig->getActiveProfile()["filters"]["pro_mode"]["accept_pilot_squawk"].GetBool()) {
					return true;
				}*/

				ASSERT(strlen(rt.GetPosition().GetSquawk()) == 4);
				if (strcmp(rt.GetPosition().GetSquawk(), "1000") == 0) { // squawk 1000
					return true;
				}

				if (strcmp(rt.GetPosition().GetSquawk() - 2, "00") == 0) { // are the last 2 chars of the squawk 00
					return false;
				}

				/*const Value& sqs = CurrentConfig->getActiveProfile()["filters"]["pro_mode"]["do_not_autocorrelate_squawks"];
				for (SizeType i = 0; i < sqs.Size(); i++) {
					if (strcmp(rt.GetPosition().GetSquawk(), sqs[i].GetString()) == 0) {
						isCorr = false;
						break;
					}
				}*/
				

				if (manuallyCorrelated.count(rt.GetSystemID()) > 0) {
					return true;
				}

				/*if (std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()) != ReleasedTracks.end()) {
					return false;
				}*/
			}

			return false;
		}
		else {
			// If the pro mode is not used, then the AC is always correlated
			return true;
		}
	};

	static CBString GetStandNumber(CFlightPlan fp)
	{
		if (!fp.IsValid())
			return "";

		const char* remarks = fp.GetFlightPlanData().GetRemarks();
		const char* pGate = strstr(remarks, " STAND/");
		if (pGate != nullptr) {
			auto pEnd = strpbrk(pGate+1, " \r\n\0");
			char stand[16];
			strncpy_s(stand, pGate + 7, pEnd - (pGate + 7));
			return stand;
		}
		return "";
	}

	static void SetStandNumber(CFlightPlan fp, CBString stand)
	{
		if (!fp.IsValid())
			return;

		CBString remarks = fp.GetFlightPlanData().GetRemarks();

		int pos1 = remarks.find(" STAND/");
		if (pos1 != BSTR_ERR) { // contains a stand already
			int pos2 = remarks.findchr(" \t\r\n\0", pos1+1);

			if (pos2 == BSTR_ERR) { // we assume that we reached end of line and so none of the above characters were found (\0 is not part of the bstring)
				pos2 = remarks.length();
			}

			if (stand == "") { // remove it
				remarks.remove(pos1, pos2 - pos1);
			}
			else { // update it
				remarks.replace(pos1, pos2 - pos1, " STAND/" + stand);
			}
		}

		else { // no entry
			if (stand != "") {
				remarks += " STAND/" + stand;
			}
		}

		fp.GetFlightPlanData().SetRemarks(remarks);
		fp.GetFlightPlanData().AmendFlightPlan();
	}

	bool IsAcOnRunway(CRadarTarget Ac)
	{
		int AltitudeDif = Ac.GetPosition().GetFlightLevel() - Ac.GetPreviousPosition(Ac.GetPosition()).GetFlightLevel();
		if (!Ac.GetPosition().GetTransponderC())
			AltitudeDif = 0;

		if (Ac.GetGS() > 160 || AltitudeDif > 200)
			return false;

		POINT AcPosPix = ConvertCoordFromPositionToPixel(Ac.GetPosition().GetPosition());

		for (const auto &rwy : RimcasInstance->Runways) {
			if (!rwy.monitor_dep && !rwy.monitor_arr)
				continue;

			vector<POINT> RunwayOnScreen;
			for (const auto &Point : rwy.rimcas_path) {
				RunwayOnScreen.push_back(ConvertCoordFromPositionToPixel(Point));
			}

			if (Is_Inside(AcPosPix, RunwayOnScreen)) {
				return true;
			}
		}

		return false;
	}


	void SMRSetCursor(HCURSOR targetCursor);

	void CorrelateCursor();
	void LoadCustomFont();
	void LoadProfile(CBString profileName);

	virtual void OnAsrContentLoaded(bool Loaded);
	virtual void OnAsrContentToBeSaved();

	virtual void OnRefresh(HDC hDC, int Phase);

	virtual void OnClickScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, int Button);
	virtual void OnDoubleClickScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, int Button);
	virtual void OnMoveScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, bool Released);
	virtual void OnOverScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area);

	virtual bool OnCompileCommand(const char * sCommandLine);
	
	virtual void OnRadarTargetPositionUpdate(CRadarTarget RadarTarget);
	virtual void OnFlightPlanDisconnect(CFlightPlan FlightPlan);

	bool isVisible(CRadarTarget rt)
	{
		CRadarTargetPositionData RtPos = rt.GetPosition();
		int radarRange = CurrentConfig->getActiveProfile()["filters"]["radar_range_nm"].GetInt();
		int altitudeFilter = CurrentConfig->getActiveProfile()["filters"]["hide_above_alt"].GetInt();
		int speedFilter = CurrentConfig->getActiveProfile()["filters"]["hide_above_spd"].GetInt();
		bool isAcDisplayed = true;

		if (AirportPositions[ActiveAirport].DistanceTo(RtPos.GetPosition()) > radarRange)
			isAcDisplayed = false;

		if (altitudeFilter != 0) {
			if (RtPos.GetPressureAltitude() > altitudeFilter)
				isAcDisplayed = false;
		}

		if (speedFilter != 0) {
			if (RtPos.GetReportedGS() > speedFilter)
				isAcDisplayed = false;
		}

		return isAcDisplayed;
	}

	//---Haversine---------------------------------------------
	// Heading in deg, distance in m
	const double PI = (double)M_PI;

	inline CPosition Haversine(CPosition origin, double heading, double distance)
	{

		CPosition newPos;

		double d = (distance*0.00053996) / 60 * PI / 180;
		double trk = DegToRad(heading);
		double lat0 = DegToRad(origin.m_Latitude);
		double lon0 = DegToRad(origin.m_Longitude);

		double lat = asin(sin(lat0) * cos(d) + cos(lat0) * sin(d) * cos(trk));
		double lon = cos(lat) == 0 ? lon0 : fmod(lon0 + asin(sin(trk) * sin(d) / cos(lat)) + PI, 2 * PI) - PI;

		newPos.m_Latitude = RadToDeg(lat);
		newPos.m_Longitude = RadToDeg(lon);

		return newPos;
	}

	inline float randomizeHeading(float originHead)
	{
		return float(fmod(originHead + float((rand() % 5) - 2), 360));
	}

	//---GetBottomLine---------------------------------------------

	virtual CBString GetBottomLine(const char * Callsign);

	void ReloadActiveRunways();

	//---OnFunctionCall-------------------------------------------------

	virtual void OnFunctionCall(int FunctionId, const char * sItemString, POINT Pt, RECT Area);

	//---OnAsrContentToBeClosed-----------------------------------------

	void CSMRRadar::EuroScopePlugInExitCustom();

	//  This gets called before OnAsrContentToBeSaved()
	// -> we can't delete CurrentConfig just yet otherwise we can't save the active profile
	inline virtual void OnAsrContentToBeClosed(void)
	{
		delete RimcasInstance;
		//delete CurrentConfig;
		delete this;
	};
};
