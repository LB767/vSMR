#include "stdafx.h"
//#include <d2d1.h>
#include "Resource.h"
#include "SMRRadar.hpp"

//#pragma comment(lib, "d2d1.lib")

ULONG_PTR m_gdiplusToken;
CPoint mouseLocation(0, 0);
CBString TagBeingDragged;

int tagLineDefaultLength = 50;
bool onFunctionCallDoubleCallHack;

//
// Cursor Things
//

bool initCursor = true;
bool useCustomCursor; // use SMR version or default windows mouse symbol
HCURSOR cursor = NULL; // The current active cursor

HCURSOR defaultCursor; // windows mouse cursor
HCURSOR smrCursor; // smr mouse cursor
HCURSOR moveCursor;
HCURSOR resizeCursor;
HCURSOR tagCursor;
HCURSOR correlateCursor;
HCURSOR mouseCursor; // default windows or SMR mouse cursor depending on user setting

WNDPROC gSourceProc;
HWND pluginWindow;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

set<CBString> CSMRRadar::manuallyCorrelated;
map<CBString, CBString> CSMRRadar::vStripsStands;
bool CSMRRadar::onFunctionCallDoubleCallHack = false;


map<int, CInsetWindow *> appWindows;

inline double closest(std::vector<double> const& vec, double value)
{
	auto const it = std::lower_bound(vec.begin(), vec.end(), value);
	if (it == vec.end()) { return -1; }

	return *it;
};
inline bool IsTagBeingDragged(CBString c)
{
	return TagBeingDragged == c;
}
bool mouseWithin(CRect rect)
{
	if (mouseLocation.x >= rect.left + 1 && mouseLocation.x <= rect.right - 1 && mouseLocation.y >= rect.top + 1 && mouseLocation.y <= rect.bottom - 1)
		return true;
	return false;
}

// ReSharper disable CppMsExtAddressOfClassRValue

CSMRRadar::CSMRRadar()
{
	Logger::info("CSMRRadar::CSMRRadar()");

	// Initializing randomizer
	srand(static_cast<unsigned>(time(nullptr)));

	// Initialize GDI+
	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);


	ConfigPath = Logger::DLL_PATH + "vSMR_Profiles.json";

	Logger::info("Loading callsigns");
	// Loading up the callsigns for the bottom line
	// Search for ICAO airlines file if it already exists (usually given by the VACC)
	CBString AirlinesPath = Logger::DLL_PATH;
	for (int i = 0; i < 3; ++i) {
		AirlinesPath = AirlinesPath.midstr(0, AirlinesPath.reversefind("\\", AirlinesPath.length() - 1));
	}
	AirlinesPath += "\\ICAO\\ICAO_Airlines.txt";

	ifstream f(AirlinesPath);
	if (f.good()) {
		Callsigns = new CCallsignLookup(AirlinesPath);
	}
	else {
		Callsigns = new CCallsignLookup(Logger::DLL_PATH + "\\ICAO_Airlines.txt");
	}
	f.close();

	Logger::info("Loading RIMCAS & Config");
	// Creating the RIMCAS instance
	if (RimcasInstance == nullptr)
		RimcasInstance = new CRimcas();

	// Loading up the config file
	if (CurrentConfig == nullptr)
		CurrentConfig = new CConfig(ConfigPath);

	if (ColorManager == nullptr)
		ColorManager = new CColorManager();

	//standardCursor = true;	
	defaultCursor = (HCURSOR)::LoadCursor(NULL, IDC_ARROW);
	smrCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCURSOR), IMAGE_CURSOR, 0, 0, LR_SHARED));
	moveCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRMOVEWINDOW), IMAGE_CURSOR, 0, 0, LR_SHARED));
	resizeCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRRESIZE), IMAGE_CURSOR, 0, 0, LR_SHARED));
	tagCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRMOVETAG), IMAGE_CURSOR, 0, 0, LR_SHARED));
	correlateCursor = CopyCursor((HCURSOR)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDC_SMRCORRELATE), IMAGE_CURSOR, 0, 0, LR_SHARED));
	mouseCursor = defaultCursor;

	// Setting up the data for the 2 approach windows
	appWindowDisplays[1] = false;
	appWindowDisplays[2] = false;
	appWindows[1] = new CInsetWindow(SRW_APPWINDOW + 1);
	appWindows[2] = new CInsetWindow(SRW_APPWINDOW + 2);

	Logger::info("Loading profile");

	this->CSMRRadar::LoadProfile("Default");
}

CSMRRadar::~CSMRRadar()
{
	Logger::info(__FUNCSIG__);
	try {
		//this->OnAsrContentToBeSaved();
		//this->EuroScopePlugInExitCustom();
	}
	catch (exception &e) {
		auto msg = bformat("Error occured: %s\n", e.what());
		AfxMessageBox(bstr2cstr(msg, ' '));
	}
	// Shutting down GDI+
	GdiplusShutdown(m_gdiplusToken);
	delete CurrentConfig;
}

void CSMRRadar::SMRSetCursor(HCURSOR targetCursor)
{
	cursor = targetCursor;
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	ASSERT(targetCursor);
	SetCursor(targetCursor);
}

void CSMRRadar::CorrelateCursor()
{
	if (NeedCorrelateCursor) {
		SMRSetCursor(correlateCursor);
		//cursor = correlateCursor;
		//AFX_MANAGE_STATE(AfxGetStaticModuleState());
		//ASSERT(smrCursor);
		//SetCursor(smrCursor);
	}
	else {
		SMRSetCursor(mouseCursor);
		//cursor = mouseCursor;
		//AFX_MANAGE_STATE(AfxGetStaticModuleState());
		//ASSERT(smrCursor);
		//SetCursor(smrCursor);
	}
}

void CSMRRadar::LoadCustomFont()
{
	Logger::info(__FUNCSIG__);

	// Loading the custom font if there is one in use
	CBString font_name = CurrentConfig->getActiveProfile()["font"]["font_name"].GetString();
	wstring buffer = ToWString(font_name);

	Gdiplus::FontStyle fontStyle = Gdiplus::FontStyleRegular;
	if (strcmp(CurrentConfig->getActiveProfile()["font"]["weight"].GetString(), "Bold") == 0)
		fontStyle = Gdiplus::FontStyleBold;
	if (strcmp(CurrentConfig->getActiveProfile()["font"]["weight"].GetString(), "Italic") == 0)
		fontStyle = Gdiplus::FontStyleItalic;

	customFont = new Gdiplus::Font(buffer.c_str(), Gdiplus::REAL(currentFontSize), fontStyle, Gdiplus::UnitPixel); // @Leak
}

void CSMRRadar::LoadProfile(CBString profileName)
{
	Logger::info(__FUNCSIG__);
	// Loading the new profile
	CurrentConfig->setActiveProfile(profileName);

	// Reloading the fonts
	LoadCustomFont();

	// Load custom runways
	RimcasInstance->Runways.clear();
	CSectorElement rwy;
	for (rwy = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); rwy.IsValid(); rwy = GetPlugIn()->SectorFileElementSelectNext(rwy, SECTOR_ELEMENT_RUNWAY)) {

		if (StartsWith(ActiveAirport, rwy.GetAirportName())) {

			CRimcas::Runway smrRunway;
			smrRunway.name = CBString(rwy.GetRunwayName(0)) + "/" + CBString(rwy.GetRunwayName(1));
			smrRunway.monitor_dep = rwy.IsElementActive(true, 0) | rwy.IsElementActive(true, 1);
			smrRunway.monitor_arr = rwy.IsElementActive(false, 0) | rwy.IsElementActive(false, 1);

			if (rwy.IsElementActive(true, 0) | rwy.IsElementActive(false, 0)) {
				smrRunway.rwyInUse = rwy.GetRunwayName(0);
			}
			else if (rwy.IsElementActive(true, 1) | rwy.IsElementActive(false, 1)) {
				smrRunway.rwyInUse = rwy.GetRunwayName(1);
			}

			CPosition Left;
			rwy.GetPosition(&Left, 1);
			CPosition Right;
			rwy.GetPosition(&Right, 0);

			double bearing1 = TrueBearing(Left, Right);
			double bearing2 = TrueBearing(Right, Left);

			const Value& CustomMap = CurrentConfig->getAirportMapIfAny(ActiveAirport);

			if (CurrentConfig->isCustomRunwayAvail(ActiveAirport, rwy.GetRunwayName(0), rwy.GetRunwayName(1))) {
				const Value& Runways = CustomMap["runways"];

				if (Runways.IsArray()) {
					for (SizeType i = 0; i < Runways.Size(); i++) {
						if (StartsWith(rwy.GetRunwayName(0), Runways[i]["runway_name"].GetString()) ||
							StartsWith(rwy.GetRunwayName(1), Runways[i]["runway_name"].GetString())) {

							// Load custom path
							const Value& path = Runways[i]["path"];
							for (SizeType j = 0; j < path.Size(); j++) {
								CPosition position;
								position.LoadFromStrings(path[j][(SizeType)1].GetString(), path[j][(SizeType)0].GetString());

								smrRunway.path.push_back(position);
							}

							// Load RIMCAS path
							const Value& rimcas_path = Runways[i]["path_rimcas"];
							for (SizeType j = 0; j < rimcas_path.Size(); j++) {
								CPosition position;
								position.LoadFromStrings(rimcas_path[j][(SizeType)1].GetString(), rimcas_path[j][(SizeType)0].GetString());

								smrRunway.rimcas_path.push_back(position);
							}

							// Check if LVP path exists
							const Value& lvp_path = Runways[i]["path_lvp"];
							if (lvp_path.IsArray()) {
								for (SizeType j = 0; j < lvp_path.Size(); j++) {
									CPosition position;
									position.LoadFromStrings(lvp_path[j][(SizeType)1].GetString(), lvp_path[j][(SizeType)0].GetString());

									smrRunway.lvp_path.push_back(position);
								}
							}
							else { // same as RIMCAS path
								smrRunway.lvp_path = smrRunway.path;
							}
						}
					}
				}
			}
			else {
				smrRunway.path = RimcasInstance->GetRunwayArea(Left, Right); // Use sector file data
				smrRunway.rimcas_path = smrRunway.path;
				smrRunway.lvp_path = smrRunway.path;
			}

			RimcasInstance->Runways.push_back(smrRunway);
		}
	}
}

void CSMRRadar::OnAsrContentLoaded(bool Loaded)
{
	Logger::info(__FUNCSIG__);
	const char* p_value;

	// ReSharper disable CppZeroConstantCanBeReplacedWithNullptr
	if ((p_value = GetDataFromAsr("Airport")) != NULL)
		ActiveAirport = p_value;

	if ((p_value = GetDataFromAsr("ActiveProfile")) != NULL)
		this->LoadProfile(p_value);

	if ((p_value = GetDataFromAsr("FontSize")) != NULL) {
		currentFontSize = atoi(p_value);
		LoadCustomFont(); // update font size
	}

	if ((p_value = GetDataFromAsr("ProMode")) != NULL)
		isProMode = atoi(p_value) == 1 ? true : false;

	if ((p_value = GetDataFromAsr("AutoDeconfliction")) != NULL)
		useAutoDeconfliction = atoi(p_value) == 1 ? true : false;

	if ((p_value = GetDataFromAsr("Afterglow")) != NULL)
		Afterglow = atoi(p_value) == 1 ? true : false;

	if ((p_value = GetDataFromAsr("AppTrailsDots")) != NULL)
		Trail_App = atoi(p_value);

	if ((p_value = GetDataFromAsr("GndTrailsDots")) != NULL)
		Trail_Gnd = atoi(p_value);

	if ((p_value = GetDataFromAsr("PredictedTrackLength")) != NULL)
		predictedTrackLength = strtof(p_value, NULL);

	if ((p_value = GetDataFromAsr("PredictedTrackWidth")) != NULL)
		predictedTrackWidth = atoi(p_value);

	if ((p_value = GetDataFromAsr("PredictedTrackSpeedThreshold")) != NULL)
		predictedTrackSpeedThreshold = atoi(p_value);

	if ((p_value = GetDataFromAsr("CustomCursor")) != NULL)
		useCustomCursor = atoi(p_value) == 1 ? true : false;

	//string temp;

	for (int i = 1; i < 3; i++) {
		CBString prefix;
		prefix.format("SRW%d", i);

		if ((p_value = GetDataFromAsr(prefix + "TopLeftX")) != NULL)
			appWindows[i]->m_Area.left = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "TopLeftY")) != NULL)
			appWindows[i]->m_Area.top = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "BottomRightX")) != NULL)
			appWindows[i]->m_Area.right = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "BottomRightY")) != NULL)
			appWindows[i]->m_Area.bottom = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "OffsetX")) != NULL)
			appWindows[i]->m_Offset.x = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "OffsetY")) != NULL)
			appWindows[i]->m_Offset.y = atoi(p_value);


		if ((p_value = GetDataFromAsr(prefix + "Filter")) != NULL)
			appWindows[i]->m_AltFilter = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "Zoom")) != NULL)
			appWindows[i]->m_Zoom = strtof(p_value, NULL);

		if ((p_value = GetDataFromAsr(prefix + "Rotation")) != NULL)
			appWindows[i]->m_Rotation = strtof(p_value, NULL);

		if ((p_value = GetDataFromAsr(prefix + "Range")) != NULL)
			appWindows[i]->m_RadarRange = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "ExtendedLinesLength")) != NULL)
			appWindows[i]->m_ExtendedLinesLength = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "ExtendedLinesTickSpacing")) != NULL)
			appWindows[i]->m_ExtendedLinesTickSpacing = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "PredictedTrackLineLength")) != NULL)
			appWindows[i]->m_PredictedTrackLineLength = strtof(p_value, NULL);

		if ((p_value = GetDataFromAsr(prefix + "PredictedTrackLineWidth")) != NULL)
			appWindows[i]->m_PredictedTrackLineWidth = atoi(p_value);

		if ((p_value = GetDataFromAsr(prefix + "Display")) != NULL)
			appWindowDisplays[i] = atoi(p_value) == 1 ? true : false;
	}

	// ReSharper restore CppZeroConstantCanBeReplacedWithNullptr
}

void CSMRRadar::OnAsrContentToBeSaved()
{
	Logger::info(__FUNCSIG__);
	CBString temp;

	SaveDataToAsr("Airport", "Active airport for RIMCAS", ActiveAirport);
	SaveDataToAsr("ActiveProfile", "vSMR active profile", CurrentConfig->getActiveProfileName());

	temp.format("%d", currentFontSize);
	SaveDataToAsr("FontSize", "vSMR font size", temp);

	temp.format("%d", Afterglow);
	SaveDataToAsr("Afterglow", "vSMR Afterglow enabled", temp);

	temp.format("%d", Trail_App);
	SaveDataToAsr("AppTrailsDots", "vSMR APPR Trail Dots", temp);

	temp.format("%d", Trail_Gnd);
	SaveDataToAsr("GndTrailsDots", "vSMR GRND Trail Dots", temp);

	temp.format("%f", predictedTrackLength);
	SaveDataToAsr("PredictedTrackLength", "vSMR Predicted Track Lines", temp);

	temp.format("%d", predictedTrackWidth);
	SaveDataToAsr("PredictedTrackWidth", "vSMR Predicted Track Lines", temp);

	temp.format("%d", predictedTrackSpeedThreshold);
	SaveDataToAsr("PredictedTrackSpeedThreshold", "vSMR Predicted Track Lines", temp);

	temp.format("%d", isProMode);
	SaveDataToAsr("ProMode", "vSMR Professional mode for correlation", temp);

	temp.format("%d", useCustomCursor);
	SaveDataToAsr("CustomCursor", "vSMR Custom Mouse Cursor", temp);

	temp.format("%d", useAutoDeconfliction);
	SaveDataToAsr("AutoDeconfliction", "vSMR Tag auto deconfliction", temp);

	for (int i = 1; i < 3; i++) {
		CBString prefix;
		prefix.format("SRW%d", i);

		temp.format("%ld", appWindows[i]->m_Area.left);
		SaveDataToAsr(prefix + "TopLeftX", prefix + " position", temp);

		temp.format("%ld", appWindows[i]->m_Area.top);
		SaveDataToAsr(prefix + "TopLeftY", prefix + " position", temp);

		temp.format("%ld", appWindows[i]->m_Area.right);
		SaveDataToAsr(prefix + "BottomRightX", prefix + " position", temp);

		temp.format("%ld", appWindows[i]->m_Area.bottom);
		SaveDataToAsr(prefix + "BottomRightY", prefix + " position", temp);

		temp.format("%ld", appWindows[i]->m_Offset.x);
		SaveDataToAsr(prefix + "OffsetX", prefix + " offset", temp);

		temp.format("%ld", appWindows[i]->m_Offset.y);
		SaveDataToAsr(prefix + "OffsetY", prefix + " offset", temp);

		temp.format("%d", appWindows[i]->m_AltFilter);
		SaveDataToAsr(prefix + "Filter", prefix + " altitude filter", temp);

		temp.format("%f", appWindows[i]->m_Zoom);
		SaveDataToAsr(prefix + "Zoom", prefix + " zoom", temp);

		temp.format("%f", appWindows[i]->m_Rotation);
		SaveDataToAsr(prefix + "Rotation", prefix + " rotation", temp);

		temp.format("%d", appWindows[i]->m_RadarRange);
		SaveDataToAsr(prefix + "Range", prefix + " range", temp);

		temp.format("%d", appWindows[i]->m_ExtendedLinesLength);
		SaveDataToAsr(prefix + "ExtendedLinesLength", prefix + " extended line length", temp);

		temp.format("%d", appWindows[i]->m_ExtendedLinesTickSpacing);
		SaveDataToAsr(prefix + "ExtendedLinesTickSpacing", prefix + " extended line tick spacing", temp);

		temp.format("%f", appWindows[i]->m_PredictedTrackLineLength);
		SaveDataToAsr(prefix + "PredictedTrackLineLength", prefix + " predicted track line length", temp);

		temp.format("%d", appWindows[i]->m_PredictedTrackLineWidth);
		SaveDataToAsr(prefix + "PredictedTrackLineWidth", prefix + " predicted track line width", temp);

		CBString to_save = "0";
		if (appWindowDisplays[i])
			to_save = "1";
		SaveDataToAsr(prefix + "Display", "Display Secondary Radar Window", to_save);
	}
}

void CSMRRadar::ReloadActiveRunways()
{
	Logger::info("SMRRadar 470");
	CSectorElement rwy;
	for (rwy = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); rwy.IsValid(); rwy = GetPlugIn()->SectorFileElementSelectNext(rwy, SECTOR_ELEMENT_RUNWAY)) {
		if (StartsWith(ActiveAirport, rwy.GetAirportName())) {

			CBString name = CBString(rwy.GetRunwayName(0)) + "/" + CBString(rwy.GetRunwayName(1));
			for (auto &smrRunway : RimcasInstance->Runways) {
				if (smrRunway.name == name) {
					smrRunway.monitor_dep = rwy.IsElementActive(true, 0) | rwy.IsElementActive(true, 1);
					smrRunway.monitor_arr = rwy.IsElementActive(false, 0) | rwy.IsElementActive(false, 1);

					if (rwy.IsElementActive(true, 0) | rwy.IsElementActive(false, 0)) {
						smrRunway.rwyInUse = rwy.GetRunwayName(0);
					}
					else if (rwy.IsElementActive(true, 1) | rwy.IsElementActive(false, 1)) {
						smrRunway.rwyInUse = rwy.GetRunwayName(1);
					}

					break;
				}
			}
		}
	}
	Logger::info("SMRRadar 493");
}

void CSMRRadar::OnMoveScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, bool Released)
{
	Logger::info(__FUNCSIG__);

	if (ObjectType == SRW_APPWINDOW + 1 || ObjectType == SRW_APPWINDOW + 2) {
		int appWindowId = ObjectType - SRW_APPWINDOW;

		bool toggleCursor = appWindows[appWindowId]->OnMoveScreenObject(sObjectId, Pt, Area, Released);

		if (!toggleCursor) {
			if (strcmp(sObjectId, "topbar") == 0)
				SMRSetCursor(moveCursor);
			else if (strcmp(sObjectId, "resize") == 0)
				SMRSetCursor(resizeCursor);

			/*AFX_MANAGE_STATE(AfxGetStaticModuleState());
			ASSERT(smrCursor);
			SetCursor(smrCursor);
			standardCursor = false;*/
		}
		else {
			SMRSetCursor(mouseCursor);

			/*AFX_MANAGE_STATE(AfxGetStaticModuleState());
			ASSERT(smrCursor);
			SetCursor(smrCursor);
			standardCursor = true;*/
		}
	}

	if (ObjectType == DRAWING_TAG || ObjectType == TAG_CITEM_MANUALCORRELATE || ObjectType == TAG_CITEM_CALLSIGN || ObjectType == TAG_CITEM_FPBOX || ObjectType == TAG_CITEM_RWY ||
		ObjectType == TAG_CITEM_SID || ObjectType == TAG_CITEM_GATE || ObjectType == TAG_CITEM_NO || ObjectType == TAG_CITEM_GROUNDSTATUS || ObjectType == TAG_CITEM_SCRATCHPAD || ObjectType == TAG_CITEM_COMMTYPE) {
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);

		if (!Released) {
			SMRSetCursor(tagCursor);
			/*AFX_MANAGE_STATE(AfxGetStaticModuleState());
			ASSERT(smrCursor);
			SetCursor(smrCursor);
			standardCursor = false;		*/
		}
		else {
			SMRSetCursor(mouseCursor);
			/*AFX_MANAGE_STATE(AfxGetStaticModuleState());
			ASSERT(smrCursor);
			SetCursor(smrCursor);
			standardCursor = true;		*/
		}

		if (rt.IsValid()) {
			CRect Temp = Area;
			POINT TagCenterPix = Temp.CenterPoint();
			POINT AcPosPix = ConvertCoordFromPositionToPixel(GetPlugIn()->RadarTargetSelect(sObjectId).GetPosition().GetPosition());
			POINT CustomTag = { TagCenterPix.x - AcPosPix.x, TagCenterPix.y - AcPosPix.y };

			if (useAutoDeconfliction) {
				double angle = RadToDeg(atan2(CustomTag.y, CustomTag.x));
				angle = fmod(angle + 360, 360);
				vector<double> angles;
				for (double k = 0.0; k <= 360.0; k += 22.5)
					angles.push_back(k);

				TagAngles[sObjectId] = closest(angles, angle);
				TagLeaderLineLength[sObjectId] = max(tagLineDefaultLength, min(int(DistancePts(AcPosPix, TagCenterPix)), tagLineDefaultLength * 2));
				TagsOffsets.erase(sObjectId);
			}
			else {
				TagsOffsets[sObjectId] = CustomTag;
			}

			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));

			if (Released) {
				TagBeingDragged = "";
			}
			else {
				TagBeingDragged = sObjectId;
			}

			RequestRefresh();
		}
	}

	if (ObjectType == RIMCAS_IAW) {
		TimePopupAreas[sObjectId] = Area;

		if (!Released) {
			SMRSetCursor(moveCursor);
			/*AFX_MANAGE_STATE(AfxGetStaticModuleState());
			ASSERT(smrCursor);
			SetCursor(smrCursor);
			standardCursor = false;*/
		}
		else {
			SMRSetCursor(mouseCursor);
			/*AFX_MANAGE_STATE(AfxGetStaticModuleState());
			ASSERT(smrCursor);
			SetCursor(smrCursor);
			standardCursor = true;*/
		}
	}

	mouseLocation = Pt;
	RequestRefresh();

}

void CSMRRadar::OnOverScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area)
{
	Logger::info(__FUNCSIG__);
	mouseLocation = Pt;
	RequestRefresh();
}

void CSMRRadar::OnClickScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, int Button)
{
	Logger::info(__FUNCSIG__);
	mouseLocation = Pt;

	if (ObjectType == SRW_APPWINDOW + 1 || ObjectType == SRW_APPWINDOW + 2) {
		int appWindowId = ObjectType - SRW_APPWINDOW;

		if (strcmp(sObjectId, "close") == 0)
			appWindowDisplays[appWindowId] = false;
		else if (strcmp(sObjectId, "view") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW View", 1);

			CBString zoom;
			zoom.format("Zoom: %.1f", appWindows[appWindowId]->m_Zoom);
			CBString rotate;
			rotate.format("Rotation: %.1f", appWindows[appWindowId]->m_Rotation);

			GetPlugIn()->AddPopupListElement(zoom, "", SRW_UPDATE_ZOOM + appWindowId);
			GetPlugIn()->AddPopupListElement(rotate, "", SRW_UPDATE_ROTATE + appWindowId);

		}
		else if (strcmp(sObjectId, "filters") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW Filters", 1);

			CBString range;
			range.format("Range: %d", appWindows[appWindowId]->m_RadarRange);
			CBString filter;
			filter.format("Alt. filter: %d", appWindows[appWindowId]->m_AltFilter);

			GetPlugIn()->AddPopupListElement(range, "", SRW_UPDATE_RANGE + appWindowId);
			GetPlugIn()->AddPopupListElement(filter, "", SRW_UPDATE_FILTER + appWindowId);

		}
		else if (strcmp(sObjectId, "centerline") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW Extended Centerline", 1);

			CBString length;
			length.format("Length: %d", appWindows[appWindowId]->m_ExtendedLinesLength);
			CBString tickSpacing;
			tickSpacing.format("Tick spacing: %d", appWindows[appWindowId]->m_ExtendedLinesTickSpacing);
			GetPlugIn()->AddPopupListElement(length, "", SRW_UPDATE_CENTERLINE + appWindowId);
			GetPlugIn()->AddPopupListElement(tickSpacing, "", SRW_UPDATE_TICKSPACING + appWindowId);
		}
		else if (strcmp(sObjectId, "predictedtrackline") == 0) {
			GetPlugIn()->OpenPopupList(Area, "SRW Predicted Track Line", 1);

			CBString length;
			length.format("Length: %.1f", appWindows[appWindowId]->m_PredictedTrackLineLength);
			CBString width;
			width.format("Width: %d", appWindows[appWindowId]->m_PredictedTrackLineWidth);
			GetPlugIn()->AddPopupListElement(length, "", SRW_UPDATE_PREDICTEDLENGTH + appWindowId);
			GetPlugIn()->AddPopupListElement(width, "", SRW_UPDATE_PREDICTEDWIDTH + appWindowId);
		}
	}

	if (ObjectType == RIMCAS_ACTIVE_AIRPORT) {
		GetPlugIn()->OpenPopupEdit(Area, RIMCAS_ACTIVE_AIRPORT_FUNC, ActiveAirport);
	}

	if (ObjectType == DRAWING_BACKGROUND_CLICK) {
		if (QDMSelectEnabled) {
			if (Button == BUTTON_LEFT) {
				QDMSelectPt = Pt;
				RequestRefresh();
			}

			if (Button == BUTTON_RIGHT) {
				QDMSelectEnabled = false;
				RequestRefresh();
			}
		}

		if (QDMenabled) {
			if (Button == BUTTON_RIGHT) {
				QDMenabled = false;
				RequestRefresh();
			}
		}
	}

	if (ObjectType == RIMCAS_MENU) {

		if(strcmp(sObjectId, "GeneralMenu") == 0)
		{
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "General Menu", 1);
			GetPlugIn()->AddPopupListElement("Pro mode", "", RIMCAS_PROMODE_TOGGLE, false, int(isProMode));
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		else if (strcmp(sObjectId, "DisplayMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Display Menu", 1);
			GetPlugIn()->AddPopupListElement("Custom Cursor", "", RIMCAS_CUSTOM_CURSOR_TOGGLE, false, int(useCustomCursor));
			GetPlugIn()->AddPopupListElement("Tag Auto-Deconfliction", "", RIMCAS_AUTO_DECONFLICTION_TOGGLE, false, int(useAutoDeconfliction));
			GetPlugIn()->AddPopupListElement("QDR Fixed Reference", "", RIMCAS_QDM_TOGGLE);
			GetPlugIn()->AddPopupListElement("QDR Select Reference", "", RIMCAS_QDM_SELECT_TOGGLE);
			GetPlugIn()->AddPopupListElement("SRW 1", "", SRW_APPWINDOW + 1, false, int(appWindowDisplays[1]));
			GetPlugIn()->AddPopupListElement("SRW 2", "", SRW_APPWINDOW + 2, false, int(appWindowDisplays[2]));
			GetPlugIn()->AddPopupListElement("Profiles", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		else if (strcmp(sObjectId, "TargetMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Target", 1);
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("Label Font Size: %d", currentFontSize), ' '), "", RIMCAS_UPDATE_FONTSIZE_EDIT);
			GetPlugIn()->AddPopupListElement("Afterglow", "", RIMCAS_UPDATE_AFTERGLOW, false, int(Afterglow));
			GetPlugIn()->AddPopupListElement("GRND Trail Dots", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("APPR Trail Dots", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("Predicted Track Line Length: %.1f", predictedTrackLength), ' '), "", RIMCAS_UPDATE_PTL_LENGTH_EDIT);
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("Predicted Track Line Width: %d", predictedTrackWidth), ' '), "", RIMCAS_UPDATE_PTL_WIDTH_EDIT);
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("Predicted Track Line Speed Threshold: %d", predictedTrackSpeedThreshold), ' '), "", RIMCAS_UPDATE_PTL_SPEED_EDIT);
			GetPlugIn()->AddPopupListElement("Acquire", "", RIMCAS_UPDATE_ACQUIRE);
			GetPlugIn()->AddPopupListElement("Release", "", RIMCAS_UPDATE_RELEASE);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		else if (strcmp(sObjectId, "MapMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Maps", 1);
			GetPlugIn()->AddPopupListElement("Airport Maps", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Custom Maps", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		else if (strcmp(sObjectId, "ColourMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Colours", 1);
			GetPlugIn()->AddPopupListElement("Colour Settings", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Brightness", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		else if (strcmp(sObjectId, "RIMCASMenu") == 0) {
			Area.top = Area.top + 30;
			Area.bottom = Area.bottom + 30;

			GetPlugIn()->OpenPopupList(Area, "Alerts", 1);
			GetPlugIn()->AddPopupListElement("Conflict Alert ARR", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Conflict Alert DEP", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Runway closed", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Visibility", "", RIMCAS_OPEN_LIST);
			GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		}

		if (strcmp(sObjectId, "/") == 0) {
			if (Button == BUTTON_LEFT) {
				DistanceToolActive = !DistanceToolActive;
				if (!DistanceToolActive)
					ActiveDistance = pair<CBString, CBString>("", "");

				if (DistanceToolActive) {
					QDMenabled = false;
					QDMSelectEnabled = false;
				}
			}
			if (Button == BUTTON_RIGHT) {
				DistanceToolActive = false;
				ActiveDistance = pair<CBString, CBString>("", "");
				DistanceTools.clear();
			}

		}

	}

	if (ObjectType == DRAWING_TAG || ObjectType == DRAWING_AC_SYMBOL) {
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		//GetPlugIn()->SetASELAircraft(rt); // NOTE: This does NOT work eventhough the api says it should?
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));  // make sure the correct aircraft is selected before calling 'StartTagFunction'

		if (rt.GetCorrelatedFlightPlan().IsValid()) {
			StartTagFunction(rt.GetCallsign(), NULL, EuroScopePlugIn::TAG_ITEM_TYPE_CALLSIGN, rt.GetCallsign(), NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, Pt, Area);
		}

		// Release & correlate actions

		if (ReleaseInProgress || AcquireInProgress) {
			if (ReleaseInProgress) {
				ReleaseInProgress = NeedCorrelateCursor = false;

				//ReleasedTracks.push_back(rt.GetSystemID());

				if (manuallyCorrelated.count(rt.GetSystemID()) > 0) {
					manuallyCorrelated.erase(rt.GetSystemID());
				}
			}

			if (AcquireInProgress) {
				AcquireInProgress = NeedCorrelateCursor = false;

				manuallyCorrelated.insert(rt.GetSystemID());

				//if (std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()) != ReleasedTracks.end())
					//ReleasedTracks.erase(std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()));
			}


			CorrelateCursor();

			return;
		}

		if (ObjectType == DRAWING_AC_SYMBOL) {
			if (QDMSelectEnabled) {
				if (Button == BUTTON_LEFT) {
					QDMSelectPt = Pt;
					RequestRefresh();
				}
			}
			else if (DistanceToolActive) {
				if (ActiveDistance.first == "") {
					ActiveDistance.first = sObjectId;
				}
				else if (ActiveDistance.second == "") {
					ActiveDistance.second = sObjectId;
					DistanceTools.insert(ActiveDistance);
					ActiveDistance = pair<CBString, CBString>("", "");
					DistanceToolActive = false;
				}
				RequestRefresh();
			}
			else {
				if (TagsOffsets.find(sObjectId) != TagsOffsets.end())
					TagsOffsets.erase(sObjectId);

				if (Button == BUTTON_LEFT) {
					if (TagAngles.find(sObjectId) == TagAngles.end()) {
						TagAngles[sObjectId] = 0;
					}
					else {
						TagAngles[sObjectId] = fmod(TagAngles[sObjectId] - 22.5, 360);
					}
				}

				if (Button == BUTTON_RIGHT) {
					if (TagAngles.find(sObjectId) == TagAngles.end()) {
						TagAngles[sObjectId] = 0;
					}
					else {
						TagAngles[sObjectId] = fmod(TagAngles[sObjectId] + 22.5, 360);
					}
				}

				RequestRefresh();
			}
		}
	}

	if (ObjectType == DRAWING_AC_SYMBOL_APPWINDOW1 || ObjectType == DRAWING_AC_SYMBOL_APPWINDOW2) {
		if (DistanceToolActive) {
			if (ActiveDistance.first == "") {
				ActiveDistance.first = sObjectId;
			}
			else if (ActiveDistance.second == "") {
				ActiveDistance.second = sObjectId;
				DistanceTools.insert(ActiveDistance);
				ActiveDistance = pair<CBString, CBString>("", "");
				DistanceToolActive = false;
			}
			RequestRefresh();
		}
		else {
			if (ObjectType == DRAWING_AC_SYMBOL_APPWINDOW1)
				appWindows[1]->OnClickScreenObject(sObjectId, Pt, Button);

			if (ObjectType == DRAWING_AC_SYMBOL_APPWINDOW2)
				appWindows[2]->OnClickScreenObject(sObjectId, Pt, Button);
		}
	}

	map <const int, const int> TagObjectMiddleTypes = {
		{ TAG_CITEM_CALLSIGN, TAG_ITEM_FUNCTION_COMMUNICATION_POPUP },
	};

	map <const int, const int> TagObjectRightTypes = {
		{ TAG_CITEM_CALLSIGN, TAG_ITEM_FUNCTION_HANDOFF_POPUP_MENU },
		{ TAG_CITEM_FPBOX, TAG_ITEM_FUNCTION_OPEN_FP_DIALOG },
		{ TAG_CITEM_RWY, TAG_ITEM_FUNCTION_ASSIGNED_RUNWAY },
		{ TAG_CITEM_SID, TAG_ITEM_FUNCTION_ASSIGNED_SID },
		{ TAG_CITEM_GROUNDSTATUS, TAG_ITEM_FUNCTION_SET_GROUND_STATUS },
		{ TAG_CITEM_COMMTYPE, TAG_ITEM_FUNCTION_COMMUNICATION_POPUP }
	};

	if (Button == BUTTON_LEFT) {
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		if (rt.GetCorrelatedFlightPlan().IsValid()) {
			StartTagFunction(rt.GetCallsign(), NULL, TAG_ITEM_TYPE_CALLSIGN, rt.GetCallsign(), NULL, TAG_ITEM_FUNCTION_NO, Pt, Area);
		}
	}

	if (Button == BUTTON_MIDDLE && TagObjectMiddleTypes[ObjectType]) {
		int TagMenu = TagObjectMiddleTypes[ObjectType];
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		StartTagFunction(rt.GetCallsign(), NULL, ObjectType, rt.GetCallsign(), NULL, TagMenu, Pt, Area);
	}

	if (Button == BUTTON_RIGHT && TagObjectRightTypes[ObjectType]) {
		int TagMenu = TagObjectRightTypes[ObjectType];
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		StartTagFunction(rt.GetCallsign(), NULL, ObjectType, rt.GetCallsign(), NULL, TagMenu, Pt, Area);
	}

	if (Button == BUTTON_RIGHT && ObjectType == TAG_CITEM_GATE) {
		CFlightPlan fp = GetPlugIn()->FlightPlanSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(fp);
		GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_STAND_EDITOR, GetStandNumber(fp));
		onFunctionCallDoubleCallHack = true;
	}

	if (Button == BUTTON_RIGHT && ObjectType == TAG_CITEM_SCRATCHPAD) { // We could use the default TAG_ITEM_FUNCTION_EDIT_SCRATCH_PAD, but that sets a default small size for the editing text window, which is a bit meh
		CFlightPlan fp = GetPlugIn()->FlightPlanSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(fp);
		GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SCRATCHPAD_EDITOR, fp.GetControllerAssignedData().GetScratchPadString());
		onFunctionCallDoubleCallHack = true;
	}


	if (ObjectType == RIMCAS_DISTANCE_TOOL) {

		CBStringList s;
		s.split(sObjectId, ',');
		pair<CBString, CBString> toRemove = pair<CBString, CBString>(s.front(), s.back());

		typedef multimap<CBString, CBString>::iterator iterator;
		std::pair<iterator, iterator> iterpair = DistanceTools.equal_range(toRemove.first);

		iterator it = iterpair.first;
		for (; it != iterpair.second; ++it) {
			if (it->second == toRemove.second) {
				it = DistanceTools.erase(it);
				break;
			}
		}
	}

	RequestRefresh();
};

void CSMRRadar::OnDoubleClickScreenObject(int ObjectType, const char * sObjectId, POINT Pt, RECT Area, int Button)
{
	Logger::info(__FUNCSIG__);
	mouseLocation = Pt;

	if (ObjectType == DRAWING_TAG || ObjectType == TAG_CITEM_MANUALCORRELATE || ObjectType == TAG_CITEM_CALLSIGN || ObjectType == TAG_CITEM_FPBOX || ObjectType == TAG_CITEM_RWY || ObjectType == TAG_CITEM_SID
		|| ObjectType == TAG_CITEM_GATE || ObjectType == TAG_CITEM_NO || ObjectType == TAG_CITEM_GROUNDSTATUS || ObjectType == TAG_CITEM_SCRATCHPAD || ObjectType == TAG_CITEM_COMMTYPE) {
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));  // make sure the correct aircraft is selected before calling 'StartTagFunction'

		CBString callsign = rt.GetCallsign();
		if (tagDetailed.count(callsign)) {
			tagDetailed.erase(callsign);
		}
		else {
			tagDetailed.insert(callsign);
		}
	}
}

void CSMRRadar::OnFunctionCall(int FunctionId, const char * sItemString, POINT Pt, RECT Area)
{
	Logger::info(__FUNCSIG__);
	Logger::info(bstr2cstr(bformat("%d - %s", FunctionId, sItemString), ' '));
	mouseLocation = Pt;

	/* 	-----------------------------------------------------------------------------------------------
	This function is seemingly called twice when coming from a popup edit box
	Who the hell knows why... but it is quite problematic
	Therefore, a hack variable is used to force the "after edit" function (here TAG_FUNC_STAND_EDITOR)
	to be called only once.

	Also, both the CPlugIn AND the CRadarScreen versions of the function always get called together,
	so technically (apparently) you could have the implementation of both into just one...
	not sure what the point would be or if it's any good either, just weird all around
	----------------------------------------------------------------------------------------------- */

	if (FunctionId == TAG_FUNC_SCRATCHPAD_EDITOR) { // when finished editing
		if (onFunctionCallDoubleCallHack) {
			CFlightPlan fp = GetPlugIn()->FlightPlanSelectASEL();
			fp.GetControllerAssignedData().SetScratchPadString(sItemString);
			onFunctionCallDoubleCallHack = false;
		}
	}

	else if (FunctionId == RIMCAS_ACTIVE_AIRPORT_FUNC) {
		ActiveAirport = sItemString;
		ActiveAirport.toupper();
		LoadProfile(CurrentConfig->getActiveProfile()["name"].GetString());
		ReloadActiveRunways();
		SaveDataToAsr("Airport", "Active airport", ActiveAirport);
	}

	else if (FunctionId == RIMCAS_UPDATE_FONTSIZE_EDIT) {
		GetPlugIn()->OpenPopupEdit(Area, RIMCAS_UPDATE_FONTSIZE_EDITOR, bstr2cstr(bformat("%d", currentFontSize), ' '));
		onFunctionCallDoubleCallHack = true;
	}
	else if (FunctionId == RIMCAS_UPDATE_FONTSIZE_EDITOR) {
		if (onFunctionCallDoubleCallHack) {
			currentFontSize = atoi(sItemString);
			LoadCustomFont();
			onFunctionCallDoubleCallHack = false;
		}
	}
	else if (FunctionId == RIMCAS_UPDATE_PTL_LENGTH_EDIT) {
		GetPlugIn()->OpenPopupEdit(Area, RIMCAS_UPDATE_PTL_LENGTH_EDITOR, bstr2cstr(bformat("%.1f", predictedTrackLength), ' '));
		onFunctionCallDoubleCallHack = true;
	}
	else if (FunctionId == RIMCAS_UPDATE_PTL_LENGTH_EDITOR) {
		if (onFunctionCallDoubleCallHack) {
			predictedTrackLength = strtof(sItemString, NULL);
			onFunctionCallDoubleCallHack = false;
		}
	}
	else if (FunctionId == RIMCAS_UPDATE_PTL_WIDTH_EDIT) {
		GetPlugIn()->OpenPopupEdit(Area, RIMCAS_UPDATE_PTL_WIDTH_EDITOR, bstr2cstr(bformat("%d", predictedTrackWidth), ' '));
		onFunctionCallDoubleCallHack = true;
	}	
	else if (FunctionId == RIMCAS_UPDATE_PTL_WIDTH_EDITOR) {
		if (onFunctionCallDoubleCallHack) {
			predictedTrackWidth = atoi(sItemString);
			onFunctionCallDoubleCallHack = false;
		}
	}
	else if (FunctionId == RIMCAS_UPDATE_PTL_SPEED_EDIT) {
		GetPlugIn()->OpenPopupEdit(Area, RIMCAS_UPDATE_PTL_SPEED_EDITOR, bstr2cstr(bformat("%d", predictedTrackSpeedThreshold), ' '));
		onFunctionCallDoubleCallHack = true;
	}
	else if (FunctionId == RIMCAS_UPDATE_PTL_SPEED_EDITOR) {
		if (onFunctionCallDoubleCallHack) {
			predictedTrackSpeedThreshold = atoi(sItemString);
			onFunctionCallDoubleCallHack = false;
		}
	}

	else if (FunctionId == RIMCAS_PROMODE_TOGGLE) {
		isProMode = !isProMode;
	}

	else if (FunctionId == RIMCAS_AUTO_DECONFLICTION_TOGGLE) {
		useAutoDeconfliction = !useAutoDeconfliction;
	}

	else if (FunctionId == RIMCAS_CUSTOM_CURSOR_TOGGLE) {
		useCustomCursor = !useCustomCursor;
		if (useCustomCursor) {
			mouseCursor = smrCursor;
		}
		else {
			mouseCursor = defaultCursor;
		}
		SMRSetCursor(mouseCursor);
	}

	else if (FunctionId == RIMCAS_QDM_TOGGLE) {
		QDMenabled = !QDMenabled;
		QDMSelectEnabled = false;
	}

	else if (FunctionId == RIMCAS_QDM_SELECT_TOGGLE) {
		if (!QDMSelectEnabled) {
			QDMSelectPt = ConvertCoordFromPositionToPixel(AirportPositions[ActiveAirport]);
		}
		QDMSelectEnabled = !QDMSelectEnabled;
		QDMenabled = false;
	}

	else if (FunctionId == RIMCAS_UPDATE_PROFILE) {
		this->CSMRRadar::LoadProfile(sItemString);
		LoadCustomFont();
		SaveDataToAsr("ActiveProfile", "vSMR active profile", sItemString);

		ShowLists["Profiles"] = true;
	}

	else if (FunctionId == RIMCAS_UPDATE_BRIGHNESS) {
		if (strcmp(sItemString, "Day") == 0)
			ColorSettingsDay = true;
		else
			ColorSettingsDay = false;

		ShowLists["Colour Settings"] = true;

		RequestRefresh();
	}

	else if (FunctionId == RIMCAS_CA_ARRIVAL_FUNC) {
		RimcasInstance->ToggleMonitoredRunwayArr(sItemString);

		ShowLists["Conflict Alert ARR"] = true;

		RequestRefresh();
	}

	else if (FunctionId == RIMCAS_CA_DEPARTURE_FUNC) {
		RimcasInstance->ToggleMonitoredRunwayDep(sItemString);

		ShowLists["Conflict Alert DEP"] = true;

		RequestRefresh();
	}

	else if (FunctionId == RIMCAS_CLOSED_RUNWAYS_FUNC) {
		RimcasInstance->ToggleClosedRunway(sItemString);

		ShowLists["Runway closed"] = true;

		RequestRefresh();
	}

	else if (FunctionId == RIMCAS_OPEN_LIST) {

		ShowLists[sItemString] = true;
		ListAreas[sItemString] = Area;

		RequestRefresh();
	}

	else if (FunctionId == RIMCAS_UPDATE_LVP) {
		if (strcmp(sItemString, "Normal") == 0)
			isLVP = false;
		if (strcmp(sItemString, "Low") == 0)
			isLVP = true;

		ShowLists["Visibility"] = true;

		RequestRefresh();
	}

	else if (FunctionId == RIMCAS_UPDATE_AFTERGLOW) {
		Afterglow = !Afterglow;
	}

	else if (FunctionId == RIMCAS_UPDATE_GND_TRAIL) {
		Trail_Gnd = atoi(sItemString);

		ShowLists["GRND Trail Dots"] = true;
	}

	else if (FunctionId == RIMCAS_UPDATE_APP_TRAIL) {
		Trail_App = atoi(sItemString);

		ShowLists["APPR Trail Dots"] = true;
	}

	else if (FunctionId == RIMCAS_BRIGHTNESS_LABEL) {
		ColorManager->update_brightness("label", atoi(sItemString));
		ShowLists["Label"] = true;
	}

	else if (FunctionId == RIMCAS_BRIGHTNESS_AFTERGLOW) {
		ColorManager->update_brightness("afterglow", atoi(sItemString));
		ShowLists["Afterglow"] = true;
	}

	else if (FunctionId == RIMCAS_BRIGHTNESS_SYMBOL) {
		ColorManager->update_brightness("symbol", std::atoi(sItemString));
		ShowLists["Symbol"] = true;
	}

	else if (FunctionId == RIMCAS_UPDATE_RELEASE) {
		ReleaseInProgress = !ReleaseInProgress;
		if (ReleaseInProgress)
			AcquireInProgress = false;
		NeedCorrelateCursor = ReleaseInProgress;

		CorrelateCursor();
	}

	else if (FunctionId == RIMCAS_UPDATE_ACQUIRE) {
		AcquireInProgress = !AcquireInProgress;
		if (AcquireInProgress)
			ReleaseInProgress = false;
		NeedCorrelateCursor = AcquireInProgress;

		CorrelateCursor();
	}
		
	
	// App windows
	for (int id = 1; id <= 2; id++) {

		if (FunctionId == SRW_APPWINDOW + id) {
			appWindowDisplays[id] = !appWindowDisplays[id];
			break;
		}

		else if (FunctionId == SRW_UPDATE_ZOOM + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_ZOOM_EDITOR + id, bstr2cstr(bformat("%.1f", appWindows[id]->m_Zoom), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_ROTATE + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_ROTATE_EDITOR + id, bstr2cstr(bformat("%.1f", appWindows[id]->m_Rotation), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_FILTER + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_FILTER_EDITOR + id, bstr2cstr(bformat("%d", appWindows[id]->m_AltFilter), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_RANGE + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_RANGE_EDITOR + id, bstr2cstr(bformat("%d", appWindows[id]->m_RadarRange), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_CENTERLINE + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_CENTERLINE_EDITOR+id, bstr2cstr(bformat("%d", appWindows[id]->m_ExtendedLinesLength), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_TICKSPACING + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_TICKSPACING_EDITOR + id, bstr2cstr(bformat("%d", appWindows[id]->m_ExtendedLinesTickSpacing), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_PREDICTEDLENGTH + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_PREDICTEDLENGTH_EDITOR + id, bstr2cstr(bformat("%.1f", appWindows[id]->m_PredictedTrackLineLength), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}
		else if (FunctionId == SRW_UPDATE_PREDICTEDWIDTH + id) {
			GetPlugIn()->OpenPopupEdit(Area, TAG_FUNC_SRW_PREDICTEDWIDTH_EDITOR + id, bstr2cstr(bformat("%d", appWindows[id]->m_PredictedTrackLineWidth), ' '));
			onFunctionCallDoubleCallHack = true;
			break;
		}

		else if (FunctionId == TAG_FUNC_SRW_ZOOM_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {
				appWindows[id]->m_Zoom = strtof(sItemString, NULL);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_ROTATE_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {
				appWindows[id]->m_Rotation = strtof(sItemString, NULL);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_FILTER_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {
				appWindows[id]->m_AltFilter = atoi(sItemString);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_RANGE_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {
				appWindows[id]->m_RadarRange = atoi(sItemString);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_CENTERLINE_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {				
				appWindows[id]->m_ExtendedLinesLength = atoi(sItemString);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_TICKSPACING_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {				
				appWindows[id]->m_ExtendedLinesTickSpacing = atoi(sItemString);				
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_PREDICTEDLENGTH_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {
				appWindows[id]->m_PredictedTrackLineLength = strtof(sItemString, NULL);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}
		else if (FunctionId == TAG_FUNC_SRW_PREDICTEDWIDTH_EDITOR + id) {
			if (onFunctionCallDoubleCallHack) {
				appWindows[id]->m_PredictedTrackLineWidth = atoi(sItemString);
				onFunctionCallDoubleCallHack = false;
			}
			break;
		}

	}

}

void CSMRRadar::OnRadarTargetPositionUpdate(CRadarTarget RadarTarget)
{
	Logger::info(__FUNCSIG__);
	if (!RadarTarget.IsValid() || !RadarTarget.GetPosition().IsValid())
		return;

	CRadarTargetPositionData RtPos = RadarTarget.GetPosition();
	CBString callsign = RadarTarget.GetCallsign();

	CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);

	// Compute current target shape points
	// All units in M
	float width = 34.0f;
	float cabin_width = 4.0f;
	float length = 38.0f;

	if (fp.IsValid()) {
		char wtc = fp.GetFlightPlanData().GetAircraftWtc();

		if (wtc == 'L') {
			width = 13.0f;
			cabin_width = 2.0f;
			length = 12.0f;
		}

		if (wtc == 'H') {
			width = 61.0f;
			cabin_width = 7.0f;
			length = 64.0f;
		}

		if (wtc == 'J') {
			width = 80.0f;
			cabin_width = 7.0f;
			length = 73.0f;
		}
	}


	width = width + float((rand() % 5) - 2);
	cabin_width = cabin_width + float((rand() % 3) - 1);
	length = length + float((rand() % 5) - 2);


	float trackHead = float(RadarTarget.GetPosition().GetReportedHeadingTrueNorth());
	float inverseTrackHead = float(fmod(trackHead + 180.0f, 360));
	float leftTrackHead = float(fmod(trackHead - 90.0f, 360));
	float rightTrackHead = float(fmod(trackHead + 90.0f, 360));

	float HalfLenght = length / 2.0f;
	float HalfCabWidth = cabin_width / 2.0f;
	float HalfSpanWidth = width / 2.0f;

	// Base shape is like a deformed cross
	CPosition topMiddle = Haversine(RtPos.GetPosition(), trackHead, HalfLenght);
	CPosition topLeft = Haversine(topMiddle, leftTrackHead, HalfCabWidth);
	CPosition topRight = Haversine(topMiddle, rightTrackHead, HalfCabWidth);

	CPosition bottomMiddle = Haversine(RtPos.GetPosition(), inverseTrackHead, HalfLenght);
	CPosition bottomLeft = Haversine(bottomMiddle, leftTrackHead, HalfCabWidth);
	CPosition bottomRight = Haversine(bottomMiddle, rightTrackHead, HalfCabWidth);

	CPosition middleTopLeft = Haversine(topLeft, float(fmod(inverseTrackHead + 25.0f, 360)), 0.8f*HalfLenght);
	CPosition middleTopRight = Haversine(topRight, float(fmod(inverseTrackHead - 25.0f, 360)), 0.8f*HalfLenght);
	CPosition middleBottomLeft = Haversine(bottomLeft, float(fmod(trackHead - 15.0f, 360)), 0.8f*HalfLenght);
	CPosition middleBottomRight = Haversine(bottomRight, float(fmod(trackHead + 15.0f, 360)), 0.8f*HalfLenght);

	CPosition rightTop = Haversine(middleBottomRight, rightTrackHead, 0.7f*HalfSpanWidth);
	CPosition rightBottom = Haversine(rightTop, inverseTrackHead, cabin_width);

	CPosition leftTop = Haversine(middleBottomLeft, leftTrackHead, 0.7f*HalfSpanWidth);
	CPosition leftBottom = Haversine(leftTop, inverseTrackHead, cabin_width);

	CPosition basePoints[PATATOIDE_NUM_OUTER_POINTS];
	basePoints[0] = topLeft;
	basePoints[1] = middleTopLeft;
	basePoints[2] = leftTop;
	basePoints[3] = leftBottom;
	basePoints[4] = middleBottomLeft;
	basePoints[5] = bottomLeft;
	basePoints[6] = bottomRight;
	basePoints[7] = middleBottomRight;
	basePoints[8] = rightBottom;
	basePoints[9] = rightTop;
	basePoints[10] = middleTopRight;
	basePoints[11] = topRight;

	// 12 points total, so 11 from 0
	// ------

	// Random points between points of base shape
	CPosition currentPoints[PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS];
	for (int i = 0; i < PATATOIDE_NUM_OUTER_POINTS; i++) {

		CPosition newPoint, lastPoint, endPoint, startPoint;

		startPoint = basePoints[i];
		if (i == PATATOIDE_NUM_OUTER_POINTS - 1) endPoint = basePoints[0];
		else endPoint = basePoints[i + 1];

		double dist, rndHeading;
		dist = startPoint.DistanceTo(endPoint);

		currentPoints[i * PATATOIDE_NUM_INNER_POINTS] = startPoint;
		lastPoint = startPoint;

		for (int k = 1; k < PATATOIDE_NUM_INNER_POINTS; k++) {
			rndHeading = float(fmod(lastPoint.DirectionTo(endPoint) + (-25.0 + (rand() % 50 + 1)), 360));
			newPoint = Haversine(lastPoint, rndHeading, dist * 200);

			currentPoints[(i * PATATOIDE_NUM_INNER_POINTS) + k] = newPoint;
			lastPoint = newPoint;
		}
	}

	auto arraySize = sizeof(currentPoints); // same size for all
	if (Patatoides.count(callsign) == 0) { // If callsign is not initialised in map -> add it with all history points initialised to current points
		Patatoide_Points points;
		Patatoides[callsign] = points;
		memcpy_s(Patatoides[callsign].points, arraySize, currentPoints, arraySize);
		memcpy_s(Patatoides[callsign].history_one_points, arraySize, currentPoints, arraySize);
		memcpy_s(Patatoides[callsign].history_two_points, arraySize, currentPoints, arraySize);
		memcpy_s(Patatoides[callsign].history_three_points, arraySize, currentPoints, arraySize);
	}
	else {
		memcpy_s(Patatoides[callsign].history_three_points, arraySize, Patatoides[callsign].history_two_points, arraySize);
		memcpy_s(Patatoides[callsign].history_two_points, arraySize, Patatoides[callsign].history_one_points, arraySize);
		memcpy_s(Patatoides[callsign].history_one_points, arraySize, Patatoides[callsign].points, arraySize);
		memcpy_s(Patatoides[callsign].points, arraySize, currentPoints, arraySize);
	}

	//Patatoides[RadarTarget.GetCallsign()].points.clear();


}

CBString CSMRRadar::GetBottomLine(const char * Callsign)
{
	Logger::info(__FUNCSIG__);

	CFlightPlan fp = GetPlugIn()->FlightPlanSelect(Callsign);
	CBString to_render;

	if (fp.IsValid()) {

		CBString icao_callsign = "";
		icao_callsign.format("%.*s", 3, Callsign);
		CBString full_callsign = Callsigns->GetFullCallsign(icao_callsign);

		to_render.format("%s (%s) (%s): %s ", Callsign, full_callsign.data, fp.GetPilotName(), fp.GetFlightPlanData().GetAircraftFPType());

		if (fp.GetFlightPlanData().IsReceived()) {
			const char* assr = fp.GetControllerAssignedData().GetSquawk();
			const char* ssr = GetPlugIn()->RadarTargetSelect(fp.GetCallsign()).GetPosition().GetSquawk();
			const char* fpType = fp.GetFlightPlanData().GetPlanType();

			if (strlen(assr) != 0 && !StartsWith(ssr, assr)) {		
				to_render.formata("%s:%s (%s)", fpType, ssr, assr);
			}
			else {
				to_render.formata("%s:%s", fpType, ssr);
			}

			to_render.formata(" %s==>%s (%s) at ", fp.GetFlightPlanData().GetOrigin(), fp.GetFlightPlanData().GetDestination(), fp.GetFlightPlanData().GetAlternate());

			int rfl = fp.GetControllerAssignedData().GetFinalAltitude();
			if (rfl == 0)
				rfl = fp.GetFlightPlanData().GetFinalAltitude();

			if (rfl > GetPlugIn()->GetTransitionAltitude())
				to_render.formata("FL%d", rfl / 100);				
			else
				to_render.formata("%dft", rfl);

			to_render.formata(" Route: %s", fp.GetFlightPlanData().GetRoute());
		}
	}

	return to_render;
}

bool CSMRRadar::OnCompileCommand(const char * sCommandLine)
{
	Logger::info(__FUNCSIG__);
	if (strcmp(sCommandLine, ".smr reload") == 0) {
		CurrentConfig = new CConfig(ConfigPath);
		LoadProfile(CurrentConfig->getActiveProfileName());
		return true;
	}

	return false;
}

map<CBString, CSMRRadar::TagItem> CSMRRadar::GenerateTagData(CRadarTarget rt, CFlightPlan fp, CSMRRadar* radar, CBString ActiveAirport)
{
	Logger::info(__FUNCSIG__);
	// ----
	// Tag items available
	// callsign: Callsign with freq state and comm *
	// actype: Aircraft type *
	// sctype: Aircraft type that changes for squawk error *
	// sqerror: Squawk error if there is one, or empty *
	// deprwy: Departure runway *
	// seprwy: Departure runway that changes to speed if speed > 25kts *
	// arvrwy: Arrival runway *
	// srvrwy: Speed that changes to arrival runway if speed < 25kts *
	// gate: Gate, from speed or scratchpad *
	// sate: Gate, from speed or scratchpad that changes to speed if speed > 25kts *
	// flightlevel: Flightlevel/Pressure altitude of the ac *
	// gs: Ground speed of the ac *
	// tendency: Climbing or descending symbol *
	// wake: Wake turbulance cat *
	// groundstatus: Current status *
	// ssr: the current squawk of the ac
	// sid: the assigned SID
	// ssid: a short version of the SID
	// origin: origin aerodrome
	// dest: destination aerodrome
	// ----

	bool isAcCorrelated = radar->IsCorrelated(fp, rt);
	int TransitionAltitude = radar->GetPlugIn()->GetTransitionAltitude();
	//bool useSpeedForGates = radar->CurrentConfig->getActiveProfile()["labels"]["use_aspeed_for_gate"].GetBool();

	bool IsPrimary = !rt.GetPosition().GetTransponderC();
	bool isAirborne = rt.GetPosition().GetPressureAltitude() > radar->CurrentConfig->getActiveProfile()["labels"]["airborne_altitude"].GetInt();
	bool isOnRunway = radar->IsAcOnRunway(rt); // radar->RimcasInstance->isAcOnRunway(callsign) <- can't use this as the list of acft on runways is cleared at this stage in the RIMCAS pipeline

	// ----- Callsign -------
	CBString callsign = rt.GetCallsign();
	if (fp.IsValid()) {
		switch (fp.GetState()) {

		case FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED:
			callsign = ">>" + callsign;
			break;

		case FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED:
			callsign = callsign + ">>";
			break;
		}
	}

	// ----- Comms (voice/receive/text)
	CBString commType = "  ";
	if (fp.IsValid()) {

		char ctrlerComType = fp.GetControllerAssignedData().GetCommunicationType();
		char fpComType = fp.GetFlightPlanData().GetCommunicationType();

		if (ctrlerComType == 't' || ctrlerComType == 'T' ||
			ctrlerComType == 'r' || ctrlerComType == 'R' ||
			ctrlerComType == 'v' || ctrlerComType == 'V') {
			if (ctrlerComType != 'v' &&	ctrlerComType != 'V') {
				commType.format("/%c", ctrlerComType);
			}
		}
		else if (fpComType == 't' || fpComType == 'T' ||
			fpComType == 'r' || fpComType == 'R') {
			commType.format("/%c", fpComType);
		}
	}


	// ----- Squawk error -------
	CBString sqerror = "";
	const char* assr = fp.GetControllerAssignedData().GetSquawk();
	const char* ssr = rt.GetPosition().GetSquawk();
	bool has_squawk_error = false;
	if (strlen(assr) != 0 && !StartsWith(ssr, assr)) {
		has_squawk_error = true;
		sqerror = "A" + CBString(assr);
	}

	// ----- Aircraft type -------
	CBString actype = ACT_TYPE_EMPTY_SPACES;
	if (fp.IsValid() && fp.GetFlightPlanData().IsReceived())
		actype = fp.GetFlightPlanData().GetAircraftFPType();
	if (actype.length() > 4 && actype != ACT_TYPE_EMPTY_SPACES) {
		actype.trunc(4);
	}

	// ----- Aircraft type that changes to squawk error -------
	CBString sctype = actype;
	if (has_squawk_error)
		sctype = sqerror;

	// ----- Groundspeed -------
	CBString speed;
	speed.format("%03d", rt.GetPosition().GetReportedGS());

	// ----- Departure runway -------
	CBString deprwy = fp.GetFlightPlanData().GetDepartureRwy();
	if (deprwy.length() == 0)
		deprwy = "RWY";

	// ----- Departure runway that changes for speed on the runway -------
	CBString seprwy = deprwy;
	if (isOnRunway) {
		seprwy.format("%03d", rt.GetPosition().GetReportedGS());
	}

	// ----- Arrival runway -------
	CBString arvrwy = fp.GetFlightPlanData().GetArrivalRwy();
	if (arvrwy.length() == 0)
		arvrwy = "RWY";

	// ----- Speed that changes to arrival runway -----
	CBString srvrwy = speed;
	if (rt.GetPosition().GetReportedGS() < 25)
		srvrwy = arvrwy;

	// ----- Speed only inside runway area -----
	CBString gshide = "";
	if (isOnRunway) {
		gshide = speed;
	}

	// ----- Gate -------
	CBString gate = CSMRRadar::GetStandNumber(fp);

	// If there is a vStrips gate, we use that
	if (vStripsStands.find(rt.GetCallsign()) != vStripsStands.end()) {
		gate = vStripsStands[rt.GetCallsign()];
	}

	if (gate.length() == 0 || gate == "0" || !isAcCorrelated)
		gate = GATE_EMPTY_SPACES;

	// ----- Gate that changes to speed -------
	CBString sate = gate;
	if (rt.GetPosition().GetReportedGS() > 25)
		sate = speed;

	// ----- Flightlevel -------
	int fl = rt.GetPosition().GetFlightLevel();
	CBString flightlevel;
	if (fl <= TransitionAltitude) {
		flightlevel.format("A%02d", rt.GetPosition().GetPressureAltitude()/100);
	}
	else {
		flightlevel.format("%03d", fl/100);
	}

	// ----- Tendency -------
	CBString tendency = "-";
	int delta_fl = rt.GetPosition().GetFlightLevel() - rt.GetPreviousPosition(rt.GetPosition()).GetFlightLevel();
	if (abs(delta_fl) >= 50) {
		if (delta_fl < 0) {
			tendency = "|";
		}
		else {
			tendency = "^";
		}
	}

	// ----- Wake cat -------
	CBString wake = "?";
	if (fp.IsValid() && isAcCorrelated) {
		wake = "";
		wake += fp.GetFlightPlanData().GetAircraftWtc();
	}

	// ----- SSR -------
	CBString tssr = "";
	if (rt.IsValid()) {
		tssr = rt.GetPosition().GetSquawk();
	}

	// ----- SID -------
	CBString dep = "SID";
	if (fp.IsValid() && isAcCorrelated) {
		dep = fp.GetFlightPlanData().GetSidName();
	}

	// ----- Short SID -------
	CBString ssid = dep;
	if (fp.IsValid() && ssid.length() > 5 && isAcCorrelated) {
		ssid.trunc(3);
		ssid += dep.midstr(dep.length() - 2, dep.length());
	}

	// ------- Origin aerodrome -------
	CBString origin = "????";
	if (isAcCorrelated) {
		origin = fp.GetFlightPlanData().GetOrigin();
	}

	// ------- Destination aerodrome -------
	CBString dest = "????";
	if (isAcCorrelated) {
		dest = fp.GetFlightPlanData().GetDestination();
	}

	// ----- GSTAT -------
	CBString gstat = "";
	if (fp.IsValid() && isAcCorrelated) {
		gstat = fp.GetGroundState();
	}
	if (gstat == "") {
		gstat = "STS";
	}

	// ----- Scratchpad ------
	CBString scratchpad = fp.GetControllerAssignedData().GetScratchPadString();
	if (scratchpad.length() == 0) {
		scratchpad = SCRATCHPAD_EMPTY_SPACES;
	}

	// ----- Generating the replacing map -----
	map<CBString, TagItem> TagMap;

	// System ID for uncorrelated
	CBString tpss = rt.GetSystemID();
	TagMap["systemid"].value += "T:" + tpss.midstr(1, 6);
	TagMap["systemid"].function = TAG_CITEM_FPBOX;

	// Pro mode data here
	//if (isProMode) {
		/*
		if (isAirborne && !isAcCorrelated) {
			callsign = tssr;
		}

		if (!isAcCorrelated) {
			actype = "NoFPL";
		}

		// Is a primary target
		if (isAirborne && !isAcCorrelated && IsPrimary) {
			flightlevel = "NoALT";
			tendency = "?";
			speed = CBString(rt.GetGS());
		}

		if (isAirborne && !isAcCorrelated && IsPrimary) {
			callsign = TagReplacingMap["systemid"];
		}
		*/
		//}

	TagMap["callsign"] = { callsign, TAG_CITEM_CALLSIGN };
	TagMap["actype"] = { actype, TAG_CITEM_FPBOX };
	TagMap["sctype"] = { sctype, TAG_CITEM_FPBOX };
	TagMap["sqerror"] = { sqerror, TAG_CITEM_FPBOX };
	TagMap["deprwy"] = { deprwy, TAG_CITEM_RWY };
	TagMap["seprwy"] = { seprwy, TAG_CITEM_RWY };
	TagMap["arvrwy"] = { arvrwy, TAG_CITEM_RWY };
	TagMap["srvrwy"] = { srvrwy, TAG_CITEM_RWY };
	TagMap["gate"] = { gate, TAG_CITEM_GATE };
	TagMap["sate"] = { sate, TAG_CITEM_GATE };
	TagMap["flightlevel"] = { flightlevel, TAG_CITEM_NO };
	TagMap["gs"] = { speed, TAG_CITEM_NO };
	TagMap["gshide"] = { gshide, TAG_CITEM_NO };
	TagMap["tendency"] = { tendency, TAG_CITEM_NO };
	TagMap["wake"] = { wake, TAG_CITEM_FPBOX };
	TagMap["ssr"] = { tssr, TAG_CITEM_NO };
	TagMap["asid"] = { dep, TAG_CITEM_SID };
	TagMap["ssid"] = { ssid,TAG_CITEM_SID };
	TagMap["origin"] = { origin, TAG_CITEM_FPBOX };
	TagMap["dest"] = { dest, TAG_CITEM_FPBOX };
	TagMap["scratchpad"] = { scratchpad, TAG_CITEM_SCRATCHPAD };
	TagMap["groundstatus"] = { gstat, TAG_CITEM_GROUNDSTATUS };
	TagMap["comms"] = { commType, TAG_CITEM_COMMTYPE };

	return TagMap;
}

void CSMRRadar::OnFlightPlanDisconnect(CFlightPlan FlightPlan)
{
	Logger::info(__FUNCSIG__);
	CBString callsign = FlightPlan.GetCallsign();

	for (multimap<CBString, CBString>::iterator itr = DistanceTools.begin(); itr != DistanceTools.end(); ++itr) {
		if (itr->first == callsign || itr->second == callsign)
			itr = DistanceTools.erase(itr);
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_SETCURSOR:
		SetCursor(cursor);
		return true;
	default:
		return CallWindowProc(gSourceProc, hwnd, uMsg, wParam, lParam);
	}
}

void CSMRRadar::OnRefresh(HDC hDC, int Phase)
{
	/*
	if (Phase != REFRESH_PHASE_BEFORE_TAGS)
		return;

	Logger::info("Direct2D init");
	ID2D1Factory* pD2DFactory = NULL;
	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);

	// Obtain the size of the drawing area.
	//CRect FullScreenArea(GetRadarArea());
	RECT rc(GetRadarArea());
	//GetClientRect(WindowFromDC(hDC), &rc);
	

	// Create a Direct2D render target          
	ID2D1DCRenderTarget* pDCRT = NULL;
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED),
		0,
		0,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
	);
	defer(SafeRelease(&pDCRT));

	hr = pD2DFactory->CreateDCRenderTarget(&props, &pDCRT);

	if (SUCCEEDED(hr)) {
		Logger::info("Direct2D successfully set up");
		hr = pDCRT->BindDC(hDC, &rc); // Bind to the DC

		pDCRT->BeginDraw();
		pDCRT->SetTransform(D2D1::Matrix3x2F::Identity());
		pDCRT->Clear(D2D1::ColorF(D2D1::ColorF::White));

		ID2D1SolidColorBrush* pBlackBrush = NULL;
		pDCRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBlackBrush);

		pDCRT->DrawEllipse(
			D2D1::Ellipse(
				D2D1::Point2F(150.0f, 150.0f),
				100.0f,
				100.0f),
			pBlackBrush,
			3.0
		);
		hr = pDCRT->EndDraw();

		SafeRelease(&pBlackBrush);
	}
	return;
	*/

	//for (int tt = 0; tt < 100000; tt++) {
		// Memory leak checker, put whatever you think might leak here and monitor RAM		
	//}


	Logger::info(__FUNCSIG__);
	// Changing the mouse cursor
	if (initCursor) {
		if (useCustomCursor) {
			mouseCursor = smrCursor;
		}
		else {
			mouseCursor = defaultCursor;
		}
		SMRSetCursor(mouseCursor);
		ASSERT(cursor != nullptr);
		pluginWindow = GetActiveWindow();
		gSourceProc = (WNDPROC)SetWindowLong(pluginWindow, GWL_WNDPROC, (LONG)WindowProc);
		initCursor = false;
	}

	if (Phase == REFRESH_PHASE_AFTER_LISTS) {
		Logger::info("Phase == REFRESH_PHASE_AFTER_LISTS");
		if (!ColorSettingsDay) {
			// Creating the gdi+ graphics
			Gdiplus::Graphics graphics(hDC);
			graphics.SetPageUnit(Gdiplus::UnitPixel);
			graphics.SetSmoothingMode(SmoothingModeAntiAlias);
			// https://docs.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-gdi-interoperation-overview

			SolidBrush AlphaBrush(Color(CurrentConfig->getActiveProfile()["filters"]["night_alpha_setting"].GetInt(), 0, 0, 0));

			CRect FullScreenArea(GetRadarArea());
			FullScreenArea.top = FullScreenArea.top - 1;
			FullScreenArea.bottom = GetChatArea().bottom;
			graphics.FillRectangle(&AlphaBrush, CopyRect(FullScreenArea));

			graphics.ReleaseHDC(hDC);
		}

		Logger::info("break Phase == REFRESH_PHASE_AFTER_LISTS");
		return;
	}

	if (Phase != REFRESH_PHASE_BEFORE_TAGS)
		return;

	Logger::info("Phase == REFRESH_PHASE_BEFORE_TAGS");

	// Timer each seconds
	clock_final = clock() - clock_init;
	double delta_t = (double)clock_final / ((double)CLOCKS_PER_SEC);
	if (delta_t >= 1) {
		clock_init = clock();
		BLINK = !BLINK;
	}

	if (!QDMenabled && !QDMSelectEnabled) {
		POINT p;
		if (GetCursorPos(&p)) {
			if (ScreenToClient(GetActiveWindow(), &p)) {
				mouseLocation = p;
			}
		}
	}

	Logger::info("Graphics set up");
	CDC dc;
	dc.Attach(hDC);

	// Set a clipping region to not draw over the chatbox
	ExcludeClipRect(dc, GetChatArea().left, GetChatArea().top, GetChatArea().right, GetChatArea().bottom);

	// Creating the gdi+ graphics
	Gdiplus::Graphics graphics(hDC);
	graphics.SetPageUnit(UnitPixel);
	graphics.SetSmoothingMode(SmoothingModeAntiAlias);

	Logger::info("SMRRadar 1938");
	AirportPositions.clear();
	for (CSectorElement apt = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT);
		apt.IsValid();
		apt = GetPlugIn()->SectorFileElementSelectNext(apt, SECTOR_ELEMENT_AIRPORT)) {
		CPosition Pos;
		apt.GetPosition(&Pos, 0);
		AirportPositions[apt.GetName()] = Pos;
	}

	Logger::info("SMRRadar 1948");
	if (QDMSelectEnabled || QDMenabled) {
		CRect R(GetRadarArea());
		R.top += 20;
		R.bottom = GetChatArea().top;

		R.NormalizeRect();
		AddScreenObject(DRAWING_BACKGROUND_CLICK, "", R, false, "");
	}

	Logger::info("SMRRadar 1958");
	for (auto runway : RimcasInstance->Runways) {
		if (runway.closed) { // if runway is closed

			//CPen RedPen(PS_SOLID, 2, RGB(150, 0, 0));
			//CPen * oldPen = dc.SelectObject(&RedPen);

			PointF lpPoints[128];
			int w = 0;
			for (auto &Point : runway.path) {
				POINT toDraw = ConvertCoordFromPositionToPixel(Point);

				lpPoints[w] = { REAL(toDraw.x), REAL(toDraw.y) };
				w++;
			}

			graphics.FillPolygon(&SolidBrush(Color(150, 0, 0)), lpPoints, w);

			//dc.SelectObject(oldPen);
		}
	}

	Logger::info("SMRRadar 1979");
	RimcasInstance->OnRefreshBegin(isLVP);

    // ---------------
	// Drawing targets
	// ---------------
	Logger::info("SMRRadar 1986");
	DrawTargets(&graphics, &dc, nullptr);

	// ---------------
	// RIMCAS 
	// ---------------
	Logger::info("SMRRadar 1992");
	RimcasInstance->OnRefreshEnd(this, CurrentConfig->getActiveProfile()["rimcas"]["rimcas_stage_two_speed_threshold"].GetInt());

	// --------------
	// Drawing Tags
	// --------------
	Logger::info("SMRRadar 1998");
	DrawTags(&graphics, nullptr);

	// --------------
	// Tag deconfliction
	// --------------
	if (useAutoDeconfliction) {
		TagDeconflict();
	}

	Logger::info("SMRRadar 2008");
	RimcasInstance->AcOnRunway.clear();
	ColorAC.clear();
	tagAreas.clear();

	graphics.SetSmoothingMode(SmoothingModeDefault);

	// Releasing the hDC after the drawing
	graphics.ReleaseHDC(hDC);

	// --------------
	// Drawing IAW
	// --------------
	Logger::info("SMRRadar 2021");
	int oldDC = SaveDC(dc);

	Logger::info("RIMCAS Loop");
	for (const auto &rwy : RimcasInstance->Runways) {
		if (!rwy.monitor_arr)
			continue;

		if (RimcasInstance->IAWQueue[rwy.name].empty())
			continue;

		if (TimePopupAreas.find(rwy.name) == TimePopupAreas.end())
			TimePopupAreas[rwy.name] = { 300, 300, 380, 380 };

		Logger::info("SMRRadar 2037");
		CRect baseRect = TimePopupAreas[rwy.name];
		baseRect.NormalizeRect();
		POINT center = baseRect.CenterPoint();

		// Drawing the runway name
		const char* tempS = rwy.rwyInUse;
		dc.SetTextColor(RGB(255, 255, 255));
		dc.TextOutA(center.x - dc.GetTextExtent(tempS).cx / 2, center.y - dc.GetTextExtent(tempS).cy / 2, tempS);

		// Drawing arcs
		CRect outerRect(baseRect);
		CRect innerRect(baseRect);
		innerRect.DeflateRect(20, 20);

		POINT arcInnerStart, arcInnerEnd, arcOuterStart, arcOuterEnd;
		arcInnerStart.x = center.x;
		arcInnerStart.y = innerRect.bottom;
		arcOuterEnd.x = center.x;
		arcOuterEnd.y = outerRect.bottom;

		Logger::info("SMRRadar 2058");
		for (const auto &aircraft : RimcasInstance->IAWQueue[rwy.name]) {

			double arcAngle = aircraft.time / 90 * M_PI; // [0-180sec] -> [0-2Pi]
			double sinAngle = sin(arcAngle);
			double cosAngle = cos(arcAngle);

			arcInnerEnd.x = center.x - LONG(sinAngle*innerRect.Width() / 2.0);
			arcInnerEnd.y = center.y + LONG(cosAngle*innerRect.Height() / 2.0);

			arcOuterStart.x = center.x - LONG(sinAngle*outerRect.Width() / 2.0);
			arcOuterStart.y = center.y + LONG(cosAngle*outerRect.Height() / 2.0);

			// Draw an arc segment
			CPen arcPen(PS_SOLID, 2, aircraft.colors.second);
			CBrush arcBrush(aircraft.colors.first);
			//defer(DeleteObject(arcPen)); // These don't actually leak
			//defer(DeleteObject(arcBrush));

			dc.SelectObject(arcPen);
			dc.SelectObject(arcBrush);

			dc.MoveTo(arcInnerStart);
			dc.BeginPath();
			dc.SetArcDirection(AD_CLOCKWISE);
			dc.ArcTo(&innerRect, arcInnerStart, arcInnerEnd);
			//dc.LineTo(arcOuterStart); // Apparently useless, the path closes by itself
			dc.SetArcDirection(AD_COUNTERCLOCKWISE);
			dc.ArcTo(&outerRect, arcOuterStart, arcOuterEnd);
			dc.LineTo(arcInnerStart);
			dc.EndPath();

			dc.StrokeAndFillPath();

			LONG arcLineX = center.x - LONG(sinAngle*baseRect.Width()*0.65);
			LONG arcLineY = center.y + LONG(cosAngle*baseRect.Height()*0.65);
			dc.MoveTo(arcInnerEnd);
			dc.LineTo(arcLineX, arcLineY);

			auto textSize = dc.GetTextExtent((const char*)aircraft.callsign);
			LONG arcTextX = arcLineX - LONG(sinAngle*textSize.cx*0.7);
			LONG arcTextY = arcLineY + LONG(cosAngle*textSize.cy*1.2);
			dc.SetTextColor(aircraft.colors.second);
			dc.SetTextAlign(TA_CENTER);
			dc.TextOutA(arcTextX, arcTextY - textSize.cy, (const char*)aircraft.callsign);

			char distanceString[8];
			sprintf_s(distanceString, "%.1f", aircraft.distance);
			dc.TextOutA(arcTextX, arcTextY, distanceString);

			outerRect.DeflateRect(4, 4);

			double arcAngleOffset = (aircraft.time + 2) / 90 * M_PI; // We add a tiny angle offset so that the arcs don't overlap
			arcInnerStart.x = center.x - LONG(sin(arcAngleOffset)*innerRect.Width() / 2.0);
			arcInnerStart.y = center.y + LONG(cos(arcAngleOffset)*innerRect.Height() / 2.0);

			arcOuterEnd.x = center.x - LONG(sin(arcAngleOffset)*outerRect.Width() / 2.0);
			arcOuterEnd.y = center.y + LONG(cos(arcAngleOffset)*outerRect.Height() / 2.0);
		}

		AddScreenObject(RIMCAS_IAW, rwy.name, baseRect, true, "");
		Logger::info("SMRRadar 2119");
	}

	RestoreDC(dc, oldDC);

	Logger::info("Menu bar lists");

	if (ShowLists["Conflict Alert ARR"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Conflict Alert ARR"], "CA Arrival", 1);
		for (const auto &rwy : RimcasInstance->Runways) {
			GetPlugIn()->AddPopupListElement(rwy.name, "", RIMCAS_CA_ARRIVAL_FUNC, false, rwy.monitor_arr);
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Conflict Alert ARR"] = false;
	}

	if (ShowLists["Conflict Alert DEP"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Conflict Alert DEP"], "CA Departure", 1);
		for (const auto &rwy : RimcasInstance->Runways) {
			GetPlugIn()->AddPopupListElement(rwy.name, "", RIMCAS_CA_DEPARTURE_FUNC, false, rwy.monitor_dep);
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Conflict Alert DEP"] = false;
	}

	if (ShowLists["Runway closed"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Runway closed"], "Runway Closed", 1);
		for (const auto &rwy : RimcasInstance->Runways) {
			GetPlugIn()->AddPopupListElement(rwy.name, "", RIMCAS_CLOSED_RUNWAYS_FUNC, false, rwy.closed);
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Runway closed"] = false;
	}

	if (ShowLists["Visibility"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Visibility"], "Visibility", 1);
		GetPlugIn()->AddPopupListElement("Normal", "", RIMCAS_UPDATE_LVP, false, int(!isLVP));
		GetPlugIn()->AddPopupListElement("Low", "", RIMCAS_UPDATE_LVP, false, int(isLVP));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Visibility"] = false;
	}

	if (ShowLists["Profiles"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Profiles"], "Profiles", 1);
		for (auto &prof : CurrentConfig->getAllProfiles()) {
			GetPlugIn()->AddPopupListElement(prof, "", RIMCAS_UPDATE_PROFILE, false, int(CurrentConfig->isItActiveProfile(prof)));
		}
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Profiles"] = false;
	}

	if (ShowLists["Colour Settings"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Colour Settings"], "Colour Settings", 1);
		GetPlugIn()->AddPopupListElement("Day", "", RIMCAS_UPDATE_BRIGHNESS, false, int(ColorSettingsDay));
		GetPlugIn()->AddPopupListElement("Night", "", RIMCAS_UPDATE_BRIGHNESS, false, int(!ColorSettingsDay));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Colour Settings"] = false;
	}

	if (ShowLists["GRND Trail Dots"]) {
		GetPlugIn()->OpenPopupList(ListAreas["GRND Trail Dots"], "GRND Trail Dots", 1);
		GetPlugIn()->AddPopupListElement("0", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 0)));
		GetPlugIn()->AddPopupListElement("2", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 2)));
		GetPlugIn()->AddPopupListElement("4", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 4)));
		GetPlugIn()->AddPopupListElement("8", "", RIMCAS_UPDATE_GND_TRAIL, false, int(bool(Trail_Gnd == 8)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["GRND Trail Dots"] = false;
	}

	if (ShowLists["APPR Trail Dots"]) {
		GetPlugIn()->OpenPopupList(ListAreas["APPR Trail Dots"], "APPR Trail Dots", 1);
		GetPlugIn()->AddPopupListElement("0", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 0)));
		GetPlugIn()->AddPopupListElement("4", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 4)));
		GetPlugIn()->AddPopupListElement("8", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 8)));
		GetPlugIn()->AddPopupListElement("12", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 12)));
		GetPlugIn()->AddPopupListElement("16", "", RIMCAS_UPDATE_APP_TRAIL, false, int(bool(Trail_App == 16)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["APPR Trail Dots"] = false;
	}

	/*if (ShowLists["Predicted Track Line"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Predicted Track Line"], "Predicted Track Line", 1);
		GetPlugIn()->AddPopupListElement("0", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 0)));
		GetPlugIn()->AddPopupListElement("1", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 1)));
		GetPlugIn()->AddPopupListElement("2", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 2)));
		GetPlugIn()->AddPopupListElement("3", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 3)));
		GetPlugIn()->AddPopupListElement("4", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 4)));
		GetPlugIn()->AddPopupListElement("5", "", RIMCAS_UPDATE_PTL, false, int(bool(PredictedLenght == 5)));
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Predicted Track Line"] = false;
	}*/

	if (ShowLists["Brightness"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Brightness"], "Brightness", 1);
		GetPlugIn()->AddPopupListElement("Label", "", RIMCAS_OPEN_LIST, false);
		GetPlugIn()->AddPopupListElement("Symbol", "", RIMCAS_OPEN_LIST, false);
		GetPlugIn()->AddPopupListElement("Afterglow", "", RIMCAS_OPEN_LIST, false);
		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Brightness"] = false;
	}

	if (ShowLists["Label"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Label"], "Label Brightness", 1);
		for (int i = CColorManager::bounds_low(); i <= CColorManager::bounds_high(); i += 10)
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("%d", i), ' '), "", RIMCAS_BRIGHTNESS_LABEL, false, int(bool(i == ColorManager->get_brightness("label"))));

		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Label"] = false;
	}

	if (ShowLists["Symbol"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Symbol"], "Symbol Brightness", 1);
		for (int i = CColorManager::bounds_low(); i <= CColorManager::bounds_high(); i += 10)
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("%d", i), ' '), "", RIMCAS_BRIGHTNESS_SYMBOL, false, int(bool(i == ColorManager->get_brightness("symbol"))));

		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Symbol"] = false;
	}

	if (ShowLists["Afterglow"]) {
		GetPlugIn()->OpenPopupList(ListAreas["Afterglow"], "Afterglow Brightness", 1);
		for (int i = CColorManager::bounds_low(); i <= CColorManager::bounds_high(); i += 10)
			GetPlugIn()->AddPopupListElement(bstr2cstr(bformat("%d", i), ' '), "", RIMCAS_BRIGHTNESS_AFTERGLOW, false, int(bool(i == ColorManager->get_brightness("afterglow"))));

		GetPlugIn()->AddPopupListElement("Close", "", RIMCAS_CLOSE, false, 2, false, true);
		ShowLists["Afterglow"] = false;
	}

	Logger::info("QRD");

	//---------------------------------
	// QRD
	//---------------------------------

	if (QDMenabled || QDMSelectEnabled || (DistanceToolActive && ActiveDistance.first != "")) {
		CPen Pen(PS_SOLID, 1, RGB(255, 255, 255));
		CPen *oldPen = dc.SelectObject(&Pen);

		POINT AirportPos = ConvertCoordFromPositionToPixel(AirportPositions[ActiveAirport]);
		CPosition AirportCPos = AirportPositions[ActiveAirport];
		if (QDMSelectEnabled) {
			AirportPos = QDMSelectPt;
			AirportCPos = ConvertCoordFromPixelToPosition(QDMSelectPt);
		}
		if (DistanceToolActive) {
			CPosition r = GetPlugIn()->RadarTargetSelect(ActiveDistance.first).GetPosition().GetPosition();
			AirportPos = ConvertCoordFromPositionToPixel(r);
			AirportCPos = r;
		}
		dc.MoveTo(AirportPos);
		POINT point = mouseLocation;
		dc.LineTo(point);

		CPosition CursorPos = ConvertCoordFromPixelToPosition(point);
		double Distance = AirportCPos.DistanceTo(CursorPos);
		double Bearing = AirportCPos.DirectionTo(CursorPos);

		Gdiplus::Pen WhitePen(Color::White);
		graphics.DrawEllipse(&WhitePen, point.x - 5, point.y - 5, 10, 10);

		Distance = Distance / 0.00053996f;
		Distance = round(Distance * 10) / 10;
		Bearing = round(Bearing * 10) / 10;

		POINT TextPos = { point.x + 20, point.y };

		if (!DistanceToolActive) {
			char buffer[32];
			sprintf_s(buffer, "%.1f\xB0 / %.1fm", Bearing, Distance);
			COLORREF old_color = dc.SetTextColor(RGB(255, 255, 255));
			dc.TextOutA(TextPos.x, TextPos.y, buffer);
			dc.SetTextColor(old_color);
		}

		dc.SelectObject(oldPen);
		RequestRefresh();
	}

	//---------------------------------
	// Drawing distance tools
	//---------------------------------
	Logger::info("SMRRadar 2295");
	DrawDistanceTools(&graphics, &dc, nullptr);

	//---------------------------------
	// Drawing the toolbar
	//---------------------------------

	Logger::info("Menu Bar");

	COLORREF qToolBarColor = RGB(127, 122, 122);

	// Drawing the toolbar on the top
	RECT RadarArea = GetRadarArea();
	RadarArea.bottom = GetChatArea().top;
	CRect ToolBarAreaTop(RadarArea.left, RadarArea.top, RadarArea.right, RadarArea.top + 20);
	dc.FillSolidRect(ToolBarAreaTop, qToolBarColor);

	COLORREF oldTextColor = dc.SetTextColor(RGB(0, 0, 0));

	int offset = 2;
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, (const char*)ActiveAirport);
	AddScreenObject(RIMCAS_ACTIVE_AIRPORT, "ActiveAirport", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent((const char*)ActiveAirport).cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent((const char*)ActiveAirport).cy }, false, "Active Airport");
	offset += dc.GetTextExtent((const char*)ActiveAirport).cx + 10;

	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "General");
	AddScreenObject(RIMCAS_MENU, "GeneralMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("General").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("General").cy }, false, "General menu");
	offset += dc.GetTextExtent("General").cx + 10;

	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Display");
	AddScreenObject(RIMCAS_MENU, "DisplayMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Display").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("Display").cy }, false, "Display menu");
	offset += dc.GetTextExtent("Display").cx + 10;	
	
	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Target");
	AddScreenObject(RIMCAS_MENU, "TargetMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Target").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("Target").cy }, false, "Target menu");
	offset += dc.GetTextExtent("Target").cx + 10;

	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Colours");
	AddScreenObject(RIMCAS_MENU, "ColourMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Colour").cx, ToolBarAreaTop.top + 4 + dc.GetTextExtent("Colour").cy }, false, "Colour menu");
	offset += dc.GetTextExtent("Colours").cx + 10;

	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "Alerts");
	AddScreenObject(RIMCAS_MENU, "RIMCASMenu", { ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("Alerts").cx, ToolBarAreaTop.top + 4 + +dc.GetTextExtent("Alerts").cy }, false, "RIMCAS menu");
	offset += dc.GetTextExtent("Alerts").cx + 10;

	dc.TextOutA(ToolBarAreaTop.left + offset, ToolBarAreaTop.top + 4, "/");
	CRect barDistanceRect = { ToolBarAreaTop.left + offset - 2, ToolBarAreaTop.top + 4, ToolBarAreaTop.left + offset + dc.GetTextExtent("/").cx, ToolBarAreaTop.top + 4 + +dc.GetTextExtent("/").cy };
	if (DistanceToolActive) {
		graphics.DrawRectangle(&Pen(Color::White), CopyRect(barDistanceRect));
	}
	AddScreenObject(RIMCAS_MENU, "/", barDistanceRect, false, "Distance tool");

	dc.SetTextColor(oldTextColor);
	

	//
	// App windows
	//
	Logger::info("App window rendering");

	for (std::map<int, bool>::iterator it = appWindowDisplays.begin(); it != appWindowDisplays.end(); ++it) {
		if (!it->second)
			continue;

		int appWindowId = it->first;
		appWindows[appWindowId]->render(hDC, this, &graphics, mouseLocation, DistanceTools);
	}

	dc.Detach();

	Logger::info("END " + CBString(__FUNCSIG__));

}


void CSMRRadar::DrawTargets(Graphics* graphics, CDC* dc, CInsetWindow* insetWindow) 
{
	// Drawing the symbols
	Logger::info("Symbols loop");
	for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();	rt = GetPlugIn()->RadarTargetSelectNext(rt)) {
		if (!rt.IsValid() || !rt.GetPosition().IsValid())
			continue;

		if (insetWindow == nullptr && !ShouldDraw(rt))
			continue;
		if (insetWindow != nullptr) {
			if (!insetWindow->ShouldDrawInWindow(this, &rt))
				continue;
		}

		CBString callsign = rt.GetCallsign();
		RimcasInstance->OnRefresh(rt, this, IsCorrelated(GetPlugIn()->FlightPlanSelect(callsign), rt));

		CRadarTargetPositionData RtPos = rt.GetPosition();
		int reportedGS = RtPos.GetReportedGS();

		if ((insetWindow == nullptr) && 
			Afterglow && 
			CurrentConfig->getActiveProfile()["targets"]["show_primary_target"].GetBool() &&
			(rt.GetPosition().GetPressureAltitude() < CurrentConfig->getActiveProfile()["labels"]["airborne_altitude"].GetInt())) {

			PointF graphicalPoints[4][PATATOIDE_NUM_INNER_POINTS*PATATOIDE_NUM_OUTER_POINTS];

			for (int i = 0; i < PATATOIDE_NUM_INNER_POINTS*PATATOIDE_NUM_OUTER_POINTS; i++) {
				// Convert from lon/lat to pixels. Has to be done here otherwise things like zooming and panning preserve the old shape.				
				POINT hist0 = ConvertCoordFromPositionToPixel(Patatoides[callsign].points[i]);
				graphicalPoints[0][i] = { REAL(hist0.x), REAL(hist0.y) };
			}

			if (rt.GetGS() > 2) { // Only compute pixel positions if we're gonna draw them
				for (int i = 0; i < PATATOIDE_NUM_INNER_POINTS*PATATOIDE_NUM_OUTER_POINTS; i++) {
					POINT hist3 = ConvertCoordFromPositionToPixel(Patatoides[callsign].history_three_points[i]);
					POINT hist2 = ConvertCoordFromPositionToPixel(Patatoides[callsign].history_two_points[i]);
					POINT hist1 = ConvertCoordFromPositionToPixel(Patatoides[callsign].history_one_points[i]);

					graphicalPoints[3][i] = { REAL(hist3.x), REAL(hist3.y) };
					graphicalPoints[2][i] = { REAL(hist2.x), REAL(hist2.y) };
					graphicalPoints[1][i] = { REAL(hist1.x), REAL(hist1.y) };
				}
			}

			SolidBrush H_Brush(ColorManager->get_corrected_color("afterglow", CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["targets"]["history_three_color"])));
			if (rt.GetGS() > 2) { // Only draw the history shapes if the plane is moving, saves some instructions...
				graphics->FillPolygon(&H_Brush, graphicalPoints[3], PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS);

				H_Brush.SetColor(ColorManager->get_corrected_color("afterglow", CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["targets"]["history_two_color"])));
				graphics->FillPolygon(&H_Brush, graphicalPoints[2], PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS);

				H_Brush.SetColor(ColorManager->get_corrected_color("afterglow", CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["targets"]["history_one_color"])));
				graphics->FillPolygon(&H_Brush, graphicalPoints[1], PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS);
			}

			H_Brush.SetColor(ColorManager->get_corrected_color("afterglow", CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["targets"]["target_color"])));
			graphics->FillPolygon(&H_Brush, graphicalPoints[0], PATATOIDE_NUM_OUTER_POINTS*PATATOIDE_NUM_INNER_POINTS);
		}


		int TrailNumber = Trail_Gnd;
		if (reportedGS > 50)
			TrailNumber = Trail_App;

		CRadarTargetPositionData previousPos = RtPos;
		for (int j = 1; j <= TrailNumber; j++) {
			if (insetWindow == nullptr) {
				POINT pCoord = ConvertCoordFromPositionToPixel(previousPos.GetPosition());
				graphics->FillRectangle(&SolidBrush(ColorManager->get_corrected_color("symbol", Gdiplus::Color::White)),
					pCoord.x - 1, pCoord.y - 1, 2, 2);
			}
			else {
				CRect windowAreaCRect(insetWindow->m_Area);
				vector<POINT> appAreaVect = { windowAreaCRect.TopLeft(),{ windowAreaCRect.right, windowAreaCRect.top }, windowAreaCRect.BottomRight(),{ windowAreaCRect.left, windowAreaCRect.bottom } };

				POINT pCoord = insetWindow->projectPoint(previousPos.GetPosition(), AirportPositions[ActiveAirport]);
				if (Is_Inside(pCoord, appAreaVect)) {
					dc->SetPixel(pCoord, ColorManager->get_corrected_color("symbol", Color::White).ToCOLORREF());
				}
			}
			previousPos = rt.GetPreviousPosition(previousPos);
		}

		bool AcisCorrelated = IsCorrelated(GetPlugIn()->FlightPlanSelect(callsign), rt);
		if (!AcisCorrelated && reportedGS < 1 && !ReleaseInProgress && !AcquireInProgress)
			continue;

		POINT acPosPix = ConvertCoordFromPositionToPixel(RtPos.GetPosition());
		if (insetWindow != nullptr) {
			acPosPix = insetWindow->projectPoint(RtPos.GetPosition(), AirportPositions[ActiveAirport]);			
		}
		
		CPen trailPen(PS_SOLID, 1, ColorManager->get_corrected_color("symbol", Gdiplus::Color::White).ToCOLORREF());
		CPen* origPen = dc->SelectObject(&trailPen);

		if (RtPos.GetTransponderC()) {
			dc->MoveTo({ acPosPix.x, acPosPix.y - 6 });
			dc->LineTo({ acPosPix.x - 6, acPosPix.y });
			dc->LineTo({ acPosPix.x, acPosPix.y + 6 });
			dc->LineTo({ acPosPix.x + 6, acPosPix.y });
			dc->LineTo({ acPosPix.x, acPosPix.y - 6 });
		}
		else {
			dc->MoveTo(acPosPix.x, acPosPix.y);
			dc->LineTo(acPosPix.x - 4, acPosPix.y - 4);
			dc->MoveTo(acPosPix.x, acPosPix.y);
			dc->LineTo(acPosPix.x + 4, acPosPix.y - 4);
			dc->MoveTo(acPosPix.x, acPosPix.y);
			dc->LineTo(acPosPix.x - 4, acPosPix.y + 4);
			dc->MoveTo(acPosPix.x, acPosPix.y);
			dc->LineTo(acPosPix.x + 4, acPosPix.y + 4);
		}

		// Predicted Track Line
		if (insetWindow == nullptr) {
			CPen predictTrackPen(PS_SOLID, predictedTrackWidth, ColorManager->get_corrected_color("symbol", Gdiplus::Color::White).ToCOLORREF());
			dc->SelectObject(&predictTrackPen);

			if (reportedGS > predictedTrackSpeedThreshold) {
				double d = double(rt.GetPosition().GetReportedGS()*0.514444) * (predictedTrackLength * 60.0);
				CPosition PredictedEnd = BetterHarversine(rt.GetPosition().GetPosition(), rt.GetTrackHeading(), d);

				dc->MoveTo(ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition()));
				dc->LineTo(ConvertCoordFromPositionToPixel(PredictedEnd));
			}
		}
		else {
			CPen predictTrackPen(PS_SOLID, insetWindow->m_PredictedTrackLineWidth, ColorManager->get_corrected_color("symbol", Gdiplus::Color::White).ToCOLORREF());
			dc->SelectObject(&predictTrackPen);

			double d = double(rt.GetPosition().GetReportedGS()*0.514444) * (insetWindow->m_PredictedTrackLineLength * 60.0);
			CPosition PredictedEnd = BetterHarversine(rt.GetPosition().GetPosition(), rt.GetTrackHeading(), d);

			POINT startPoint = insetWindow->projectPoint(rt.GetPosition().GetPosition(), AirportPositions[ActiveAirport]);
			POINT endPoint = insetWindow->projectPoint(PredictedEnd, AirportPositions[ActiveAirport]);

			dc->MoveTo(startPoint);
			dc->LineTo(endPoint);
		}

		// Mouseover symbol
		if (mouseWithin({ acPosPix.x - 5, acPosPix.y - 5, acPosPix.x + 5, acPosPix.y + 5 })) {
			dc->MoveTo(acPosPix.x, acPosPix.y - 8);
			dc->LineTo(acPosPix.x - 6, acPosPix.y - 12);
			dc->MoveTo(acPosPix.x, acPosPix.y - 8);
			dc->LineTo(acPosPix.x + 6, acPosPix.y - 12);

			dc->MoveTo(acPosPix.x, acPosPix.y + 8);
			dc->LineTo(acPosPix.x - 6, acPosPix.y + 12);
			dc->MoveTo(acPosPix.x, acPosPix.y + 8);
			dc->LineTo(acPosPix.x + 6, acPosPix.y + 12);

			dc->MoveTo(acPosPix.x - 8, acPosPix.y);
			dc->LineTo(acPosPix.x - 12, acPosPix.y - 6);
			dc->MoveTo(acPosPix.x - 8, acPosPix.y);
			dc->LineTo(acPosPix.x - 12, acPosPix.y + 6);

			dc->MoveTo(acPosPix.x + 8, acPosPix.y);
			dc->LineTo(acPosPix.x + 12, acPosPix.y - 6);
			dc->MoveTo(acPosPix.x + 8, acPosPix.y);
			dc->LineTo(acPosPix.x + 12, acPosPix.y + 6);
		}

		if (insetWindow == nullptr) {
			AddScreenObject(DRAWING_AC_SYMBOL, callsign, { acPosPix.x - 5, acPosPix.y - 5, acPosPix.x + 5, acPosPix.y + 5 }, false, AcisCorrelated ? GetBottomLine(rt.GetCallsign()) : rt.GetSystemID());
		}
		else {
			AddScreenObject(DRAWING_AC_SYMBOL_APPWINDOW_BASE + (insetWindow->m_Id - SRW_APPWINDOW), rt.GetCallsign(), { acPosPix.x - 5, acPosPix.y - 5, acPosPix.x + 5, acPosPix.y + 5 }, false, GetBottomLine(rt.GetCallsign()));
		}
		dc->SelectObject(origPen);
	}
}

void CSMRRadar::DrawTags(Graphics* graphics, CInsetWindow* insetWindow)
{
	Logger::info("Tags loop");
	for (auto rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid(); rt = GetPlugIn()->RadarTargetSelectNext(rt)) {

		if (!rt.IsValid())
			continue;

		CFlightPlan fp = GetPlugIn()->FlightPlanSelect(rt.GetCallsign());
		int reportedGs = rt.GetPosition().GetReportedGS();

		Logger::info("SMRRadar 2552");
		// Filtering the targets
		if (insetWindow == nullptr && !ShouldDraw(rt))
			continue;

		bool AcisCorrelated = IsCorrelated(fp, rt);
		if (!AcisCorrelated && reportedGs < 3)
			continue;

		if (insetWindow != nullptr) {
			if (!insetWindow->ShouldDrawInWindow(this, &rt))
				continue;
		}

		//if (std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()) != ReleasedTracks.end())
		//isAcDisplayed = false;

		Logger::info("SMRRadar 2569");
		// Getting the tag center/offset
		POINT acPosPix;
		POINT TagCenter;
		int length = tagLineDefaultLength;

		if (insetWindow == nullptr) {
			acPosPix = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());

			map<CBString, POINT>::iterator it = TagsOffsets.find(rt.GetCallsign());
			if (it != TagsOffsets.end()) {
				TagCenter.x = acPosPix.x + it->second.x;
				TagCenter.y = acPosPix.y + it->second.y ;
			}
			else {
				// Use angle:
				if (TagAngles.find(rt.GetCallsign()) == TagAngles.end()) {
					TagAngles[rt.GetCallsign()] = 270.0f;
				}

				if (TagLeaderLineLength.find(rt.GetCallsign()) != TagLeaderLineLength.end()) {
					length = TagLeaderLineLength[rt.GetCallsign()];
				}

				TagCenter.x = long(acPosPix.x + float(length * cos(DegToRad(TagAngles[rt.GetCallsign()]))));
				TagCenter.y = long(acPosPix.y + float(length * sin(DegToRad(TagAngles[rt.GetCallsign()]))));
			}			
		}
		else {
			acPosPix = insetWindow->projectPoint(rt.GetPosition().GetPosition(), AirportPositions[ActiveAirport]);
			length = 50; // fixed length in SRW

			if (insetWindow->m_TagAngles.find(rt.GetCallsign()) == insetWindow->m_TagAngles.end()) {
				insetWindow->m_TagAngles[rt.GetCallsign()] = 45.0; // @TODO: Not the best, ah well
			}
			TagCenter.x = long(acPosPix.x + float(length * cos(DegToRad(insetWindow->m_TagAngles[rt.GetCallsign()]))));
			TagCenter.y = long(acPosPix.y + float(length * sin(DegToRad(insetWindow->m_TagAngles[rt.GetCallsign()]))));
		}

		Logger::info("SMRRadar 2608");
		TagTypes TagType = TagTypes::Departure;
		TagTypes ColorTagType = TagTypes::Departure;

		if (fp.IsValid() && StartsWith(fp.GetFlightPlanData().GetDestination(), ActiveAirport)) {
			// Circuit aircraft are treated as departures; not arrivals
			if (!StartsWith(fp.GetFlightPlanData().GetOrigin(), ActiveAirport)) {
				TagType = TagTypes::Arrival;
				ColorTagType = TagTypes::Arrival;
			}
		}

		if (rt.GetPosition().GetPressureAltitude() > CurrentConfig->getActiveProfile()["labels"]["airborne_altitude"].GetInt()) {
			TagType = TagTypes::Airborne;

			// Is "use_departure_arrival_coloring" enabled? if not, then use the airborne colors
			bool useDepArrColors = CurrentConfig->getActiveProfile()["labels"]["airborne"]["use_departure_arrival_coloring"].GetBool();
			if (!useDepArrColors) {
				ColorTagType = TagTypes::Airborne;
			}
		}

		if (!AcisCorrelated && reportedGs >= 3) {
			TagType = TagTypes::Uncorrelated;
			ColorTagType = TagTypes::Uncorrelated;
		}

		Logger::info("SMRRadar 2635");
		map<CBString, TagItem> TagMap = GenerateTagData(rt, fp, this, ActiveAirport);

		StringFormat* stformat = new Gdiplus::StringFormat();
		stformat->SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces);		
		// Look at this https://stackoverflow.com/questions/1203087/why-is-graphics-measurestring-returning-a-higher-than-expected-number

		// Get the TAG label settings
		const Value& LabelsSettings = CurrentConfig->getActiveProfile()["labels"];

		// First we need to figure out the tag size		
		RectF mesureRect;
		graphics->MeasureString(L" ", wcslen(L" "), customFont, PointF(0, 0), stformat, &mesureRect);
		auto blankWidth = mesureRect.GetRight()*(REAL)0.8;

		// default font size
		mesureRect = RectF(0, 0, 0, 0);
		graphics->MeasureString(L"AZERTYUIOPQSDFGHJKLMWXCVBN", wcslen(L"AZERTYUIOPQSDFGHJKLMWXCVBN"), customFont, PointF(0, 0), stformat, &mesureRect);
		auto oneLineHeight = mesureRect.GetBottom();

		// bigger font size if used for 1st TAG line		
		CBString font_name = CurrentConfig->getActiveProfile()["font"]["font_name"].GetString();
		wstring wide_font_name = ToWString(font_name);

		float fontsize = customFont->GetSize();
		double fontSizeScaling = 1.0;
		if (LabelsSettings[getEnumString(ColorTagType)].HasMember("first_line_font_factor")) {
			fontSizeScaling = LabelsSettings[getEnumString(ColorTagType)]["first_line_font_factor"].GetDouble();
			fontsize = round((float)fontSizeScaling * fontsize);
		}
		Gdiplus::Font firstLineFont(wide_font_name.c_str(), Gdiplus::REAL(fontsize), customFont->GetStyle(), Gdiplus::UnitPixel);
		//defer(delete(firstLineFont));

		mesureRect = RectF(0, 0, 0, 0);
		graphics->MeasureString(L"AZERTYUIOPQSDFGHJKLMWXCVBN", wcslen(L"AZERTYUIOPQSDFGHJKLMWXCVBN"), &firstLineFont, PointF(0, 0), stformat, &mesureRect);
		auto firstLineHeight = mesureRect.GetBottom();

		// get label lines definitions
		const char* def = "definition";
		if (tagDetailed.count(rt.GetCallsign()) > 0 && LabelsSettings[getEnumString(TagType)]["definition_full"].IsArray()) {
			def = "definition_full";
		}

		const Value& LabelLines = LabelsSettings[getEnumString(TagType)][def];
		vector<vector<TagItem>> ReplacedLabelLines;

		if (!LabelLines.IsArray())
			continue;
		
		Logger::info("SMRRadar 2684");
		REAL TagWidth = 0, TagHeight = 0;
		REAL TAG_PADDING_LEFT = 0;// (REAL)(currentFontSize / 4.0);
		REAL TAG_PADDING_RIGHT = 0;// TAG_PADDING_LEFT;
		REAL TAG_PADDING_TOP = 0;// TAG_PADDING_LEFT*(REAL)0.5;
		REAL TAG_PADDING_BOTTOM = 0;// TAG_PADDING_LEFT*(REAL)0.5;
		
		for (unsigned int i = 0; i < LabelLines.Size(); i++) {

			const Value& line = LabelLines[i];
			vector<TagItem> lineTagItemArray;

			// Adds one line height
			if (i == 0) {
				TagHeight += TAG_PADDING_TOP;
				TagHeight += firstLineHeight; // special case 1st line
			}
			else {
				TagHeight += oneLineHeight;
			}

			Gdiplus::REAL lineWidth = TAG_PADDING_LEFT;
			for (unsigned int j = 0; j < line.Size(); j++) {
				mesureRect = RectF(0, 0, 0, 0);
				CBString tagKey = line[j].GetString();

				lineTagItemArray.push_back(TagMap[tagKey]);

				if (tagKey == "comms") { // Pretty hacky...
					lineWidth -= blankWidth; // We don't want a space between the callsign and the comm type if it's there
				}

				wstring wstr = ToWString(TagMap[tagKey].value);
				if (i == 0) {
					graphics->MeasureString(wstr.c_str(), wcslen(wstr.c_str()), &firstLineFont, PointF(0, 0), stformat, &mesureRect); // special case for first line
				}
				else {
					graphics->MeasureString(wstr.c_str(), wcslen(wstr.c_str()), customFont, PointF(0, 0), stformat, &mesureRect);
				}
				lineWidth += mesureRect.GetRight();

				if (j != line.Size() - 1 && i != 0) {
					lineWidth += blankWidth;
				}
			}
			lineWidth += TAG_PADDING_RIGHT;
			TagWidth = max(TagWidth, lineWidth);

			ReplacedLabelLines.push_back(lineTagItemArray);
		}
		TagHeight += TAG_PADDING_BOTTOM;

		Color definedBackgroundColor = CurrentConfig->getConfigColor(LabelsSettings[getEnumString(ColorTagType)]["background_color"]);
		if (ColorTagType == TagTypes::Departure) {
			if (TagMap["asid"].value != "" && CurrentConfig->isSidColorAvail(TagMap["asid"].value, ActiveAirport)) {
				definedBackgroundColor = CurrentConfig->getSidColor(TagMap["asid"].value, ActiveAirport);
			}

			if (fp.GetFlightPlanData().GetPlanType() == "I" && TagMap["asid"].value == "" && LabelsSettings[getEnumString(ColorTagType)].HasMember("nosid_color")) {
				definedBackgroundColor = CurrentConfig->getConfigColor(LabelsSettings[getEnumString(ColorTagType)]["nosid_color"]);
			}

			if (TagMap["actype"].value == ACT_TYPE_EMPTY_SPACES && LabelsSettings[getEnumString(ColorTagType)].HasMember("nofpl_color")) {
				definedBackgroundColor = CurrentConfig->getConfigColor(LabelsSettings[getEnumString(ColorTagType)]["nofpl_color"]);
			}
		}

		Color TagBackgroundColor = RimcasInstance->GetAircraftColor(rt.GetCallsign(),
			definedBackgroundColor,
			CurrentConfig->getConfigColor(LabelsSettings[getEnumString(ColorTagType)]["background_color_on_runway"]),
			CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_one"]),
			CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_two"]));

		// We need to figure out if the tag color changes according to RIMCAS alerts, or not
		bool rimcasLabelOnly = CurrentConfig->getActiveProfile()["rimcas"]["rimcas_label_only"].GetBool();

		if (rimcasLabelOnly)
			TagBackgroundColor = RimcasInstance->GetAircraftColor(rt.GetCallsign(),
				definedBackgroundColor,
				CurrentConfig->getConfigColor(LabelsSettings[getEnumString(ColorTagType)]["background_color_on_runway"]));

		TagBackgroundColor = ColorManager->get_corrected_color("label", TagBackgroundColor);
		
		// ---------------------
		// Drawing the tag
		// ---------------------
		Logger::info("SMRRadar 2770");
		CRect TagBackgroundRect((int)(TagCenter.x - (TagWidth / 2.0)), (int)(TagCenter.y - (TagHeight / 2.0)), (int)(TagCenter.x + (TagWidth / 2.0)), (int)(TagCenter.y + (TagHeight / 2.0)));
		
		// We only draw if the tag is:
		//		1) from the normal SMR window
		//      2) inside the SRW window

		bool shouldDraw = false;
		if (insetWindow == nullptr) {
			shouldDraw = true;
		}
		else {
			CRect windowAreaCRect(insetWindow->m_Area);
			vector<POINT> appAreaVect = { windowAreaCRect.TopLeft(),{ windowAreaCRect.right, windowAreaCRect.top }, windowAreaCRect.BottomRight(),{ windowAreaCRect.left, windowAreaCRect.bottom } };

			if (Is_Inside(TagBackgroundRect.TopLeft(), appAreaVect) &&
				Is_Inside(acPosPix, appAreaVect) &&
				Is_Inside(TagBackgroundRect.BottomRight(), appAreaVect)) {
				shouldDraw = true;
			}
		}

		Logger::info("SMRRadar 2792");
		if (shouldDraw) {
			SolidBrush TagBackgroundBrush(TagBackgroundColor);
			graphics->FillRectangle(&TagBackgroundBrush, CopyRect(TagBackgroundRect));
			if (mouseWithin(TagBackgroundRect) || IsTagBeingDragged(rt.GetCallsign())) {
				Pen pw(ColorManager->get_corrected_color("label", Color::White));
				graphics->DrawRectangle(&pw, CopyRect(TagBackgroundRect));
			}
			if (TagMap["groundstatus"].value == "DEPA" && ColorTagType == TagTypes::Departure) { // White border if tag is departure
				Pen pw(ColorManager->get_corrected_color("label", Color::White), 2);
				graphics->DrawRectangle(&pw, CopyRect(TagBackgroundRect));
			}

			// Getting font colors
			SolidBrush FontColor(ColorManager->get_corrected_color("label", CurrentConfig->getConfigColor(LabelsSettings[getEnumString(ColorTagType)]["text_color"])));
			SolidBrush SquawkErrorColor(ColorManager->get_corrected_color("label", CurrentConfig->getConfigColor(LabelsSettings["squawk_error_color"])));
			SolidBrush RimcasTextColor(CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["alert_text_color"]));

			/*
			SolidBrush GroundPushColor(TagBackgroundColor);
			SolidBrush GroundTaxiColor(TagBackgroundColor);
			SolidBrush GroundDepaColor(TagBackgroundColor);
			if (LabelsSettings.HasMember("groundstatus_colors")) {
			GroundPushColor.SetColor(ColorManager->get_corrected_color("label", CurrentConfig->getConfigColor(LabelsSettings["groundstatus_colors"]["push"])));
			GroundTaxiColor.SetColor(ColorManager->get_corrected_color("label", CurrentConfig->getConfigColor(LabelsSettings["groundstatus_colors"]["taxi"])));
			GroundDepaColor.SetColor(ColorManager->get_corrected_color("label", CurrentConfig->getConfigColor(LabelsSettings["groundstatus_colors"]["depa"])));
			}
			*/

			// Drawing the tag line
			RECT TagBackRectData = TagBackgroundRect;
			POINT toDraw1, toDraw2;
			if (LiangBarsky(TagBackRectData, acPosPix, TagBackgroundRect.CenterPoint(), toDraw1, toDraw2))
				graphics->DrawLine(&Pen(ColorManager->get_corrected_color("symbol", Color::White)), PointF(Gdiplus::REAL(acPosPix.x), Gdiplus::REAL(acPosPix.y)), PointF(Gdiplus::REAL(toDraw1.x), Gdiplus::REAL(toDraw1.y)));

			// If we use a RIMCAS label only, we display it, and adapt the rectangle
			CRect oldCrectSave = TagBackgroundRect;

			if (rimcasLabelOnly) {
				Color RimcasLabelColor = RimcasInstance->GetAircraftColor(rt.GetCallsign(), Color::AliceBlue, Color::AliceBlue,
					CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_one"]),
					CurrentConfig->getConfigColor(CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_two"]));

				if (RimcasLabelColor.ToCOLORREF() != Color(Color::AliceBlue).ToCOLORREF()) {
					RimcasLabelColor = ColorManager->get_corrected_color("label", RimcasLabelColor);
					int rimcas_height = 0;

					wstring rimcasw = L"ALERT";
					RectF RectRimcas_height;
					stformat->SetAlignment(StringAlignment::StringAlignmentCenter);

					graphics->MeasureString(rimcasw.c_str(), wcslen(rimcasw.c_str()), customFont, PointF(0, 0), stformat, &RectRimcas_height);
					rimcas_height = int(RectRimcas_height.GetBottom());

					// Drawing the rectangle
					CRect RimcasLabelRect(TagBackgroundRect.left, TagBackgroundRect.top - rimcas_height, TagBackgroundRect.right, TagBackgroundRect.top);
					graphics->FillRectangle(&SolidBrush(RimcasLabelColor), CopyRect(RimcasLabelRect));
					TagBackgroundRect.top -= rimcas_height;

					// Drawing the text
					graphics->DrawString(rimcasw.c_str(), wcslen(rimcasw.c_str()), customFont, PointF(Gdiplus::REAL((TagBackgroundRect.left + TagBackgroundRect.right) / 2), Gdiplus::REAL(TagBackgroundRect.top)), stformat, &RimcasTextColor);
					stformat->SetAlignment(StringAlignment::StringAlignmentNear);
				}
			}

			// Adding the tag screen object if we're on the normal SMR window
			if (insetWindow == nullptr) {
				tagAreas[rt.GetCallsign()] = TagBackgroundRect;
				AddScreenObject(DRAWING_TAG, rt.GetCallsign(), TagBackgroundRect, true, GetBottomLine(rt.GetCallsign()));
			}
			TagBackgroundRect = oldCrectSave;

			// Draw tag text and clickable zones
			Gdiplus::REAL heightOffset = TAG_PADDING_TOP;
			for (unsigned int i = 0; i < ReplacedLabelLines.size(); i++) {

				auto line = ReplacedLabelLines[i];
				Gdiplus::REAL widthOffset = TAG_PADDING_LEFT;
				for (unsigned int j = 0; j < line.size(); j++) {
					auto tagItem = line[j];
					SolidBrush* color = &FontColor;
					if (TagMap["sqerror"].value.length() > 0 && tagItem.value == TagMap["sqerror"].value)
						color = &SquawkErrorColor;

					if (RimcasInstance->getAlert(rt.GetCallsign()) != CRimcas::NoAlert)
						color = &RimcasTextColor;

					// Ground tag colors
					/*if (tagItem.value == "PUSH")
					color = &GroundPushColor;
					if (tagItem.value == "TAXI")
					color = &GroundTaxiColor;
					if (tagItem.value == "DEPA")
					color = &GroundDepaColor;
					*/
					RectF mRect(0, 0, 0, 0);
					wstring welement = ToWString(tagItem.value);				

					if (tagItem.function == TAG_CITEM_COMMTYPE) { // Pretty hacky...
						widthOffset -= blankWidth; // We don't want a space between the callsign and the comm type if it's there
					}

					if (i == 0) { // first line
						graphics->DrawString(welement.c_str(), wcslen(welement.c_str()), &firstLineFont,
							PointF(Gdiplus::REAL(TagBackgroundRect.left) + widthOffset, Gdiplus::REAL(TagBackgroundRect.top) + heightOffset),
							stformat, color);

						graphics->MeasureString(welement.c_str(), wcslen(welement.c_str()), &firstLineFont,
							PointF(0, 0), stformat, &mRect);
					}
					else {
						graphics->DrawString(welement.c_str(), wcslen(welement.c_str()), customFont,
							PointF(Gdiplus::REAL(TagBackgroundRect.left) + widthOffset, Gdiplus::REAL(TagBackgroundRect.top) + heightOffset),
							stformat, color);

						graphics->MeasureString(welement.c_str(), wcslen(welement.c_str()), customFont,
							PointF(0, 0), stformat, &mRect);
					}

					CRect ItemRect((int)(TagBackgroundRect.left + widthOffset), (int)(TagBackgroundRect.top + heightOffset),
						(int)(TagBackgroundRect.left + widthOffset + mRect.GetRight()), (int)(TagBackgroundRect.top + heightOffset + mRect.GetBottom()));

					AddScreenObject(tagItem.function, rt.GetCallsign(), ItemRect, (insetWindow == nullptr), GetBottomLine(rt.GetCallsign()));	// Moveable sets to TRUE only in main SMR window, not insets, so we can't drag the tag from the inset

					widthOffset += mRect.GetRight();	

					if (j != line.size() - 1 && i != 0) {
						widthOffset += blankWidth;
					}
				}

				if (i == 0) {
					heightOffset += firstLineHeight;
				}
				else {
					heightOffset += oneLineHeight;
				}
				widthOffset += TAG_PADDING_RIGHT;
			}
			heightOffset += TAG_PADDING_BOTTOM;
		}
	}
}

void CSMRRadar::DrawDistanceTools(Graphics* graphics, CDC* dc, CInsetWindow* insetWindow) {

	for (auto&& kv : DistanceTools) {
		CRadarTarget one = GetPlugIn()->RadarTargetSelect(kv.first);
		CRadarTarget two = GetPlugIn()->RadarTargetSelect(kv.second);

		if (!one.IsValid() || !one.GetPosition().IsValid())
			continue;

		if (!ShouldDraw(one) || !ShouldDraw(two))
			continue;

		if (insetWindow != nullptr) {
			if (!insetWindow->ShouldDrawInWindow(this, &one) || !insetWindow->ShouldDrawInWindow(this, &two))
				continue;
		}

		CPen Pen(PS_SOLID, 1, RGB(255, 255, 255));
		CPen *oldPen = dc->SelectObject(&Pen);

		POINT onePoint;
		POINT twoPoint;

		if (insetWindow == nullptr) {
			onePoint = ConvertCoordFromPositionToPixel(one.GetPosition().GetPosition());
			twoPoint = ConvertCoordFromPositionToPixel(two.GetPosition().GetPosition());

			dc->MoveTo(onePoint);
			dc->LineTo(twoPoint);
		}
		else {
			onePoint = insetWindow->projectPoint(one.GetPosition().GetPosition(), AirportPositions[ActiveAirport]);
			twoPoint = insetWindow->projectPoint(two.GetPosition().GetPosition(), AirportPositions[ActiveAirport]);

			POINT toDraw1, toDraw2;
			if (LiangBarsky(insetWindow->m_Area, onePoint, twoPoint, toDraw1, toDraw2)) {
				dc->MoveTo(toDraw1);
				dc->LineTo(toDraw2);
			}
		}

		POINT TextPos = { twoPoint.x + 20, twoPoint.y };

		double Distance = one.GetPosition().GetPosition().DistanceTo(two.GetPosition().GetPosition());
		double Bearing = one.GetPosition().GetPosition().DirectionTo(two.GetPosition().GetPosition());

		char QDRText[32];
		sprintf_s(QDRText, "%.1f\xB0 / %.1fNM", Bearing, Distance);
		COLORREF old_color = dc->SetTextColor(RGB(0, 0, 0));

		CRect ClickableRect = { TextPos.x - 2, TextPos.y, TextPos.x + dc->GetTextExtent(QDRText).cx + 2, TextPos.y + dc->GetTextExtent(QDRText).cy };	
		
		if (insetWindow != nullptr) {
			CRect windowAreaCRect(insetWindow->m_Area);
			vector<POINT> appAreaVect = { windowAreaCRect.TopLeft(),{ windowAreaCRect.right, windowAreaCRect.top }, windowAreaCRect.BottomRight(),{ windowAreaCRect.left, windowAreaCRect.bottom } };

			if (Is_Inside(ClickableRect.TopLeft(), appAreaVect) && Is_Inside(ClickableRect.BottomRight(), appAreaVect)) {
				graphics->FillRectangle(&SolidBrush(Color(127, 122, 122)), CopyRect(ClickableRect));
				dc->Draw3dRect(ClickableRect, RGB(75, 75, 75), RGB(45, 45, 45));
				dc->TextOutA(TextPos.x, TextPos.y, QDRText);

				AddScreenObject(RIMCAS_DISTANCE_TOOL, kv.first + "," + kv.second, ClickableRect, false, "");
			}
		}

		dc->SetTextColor(old_color);

		dc->SelectObject(oldPen);
	}

}

void CSMRRadar::TagDeconflict() {

	Logger::info("Tag deconfliction loop");

	for (const auto areas : tagAreas) {		

		if (IsTagBeingDragged(areas.first))
			continue;

		if (RecentlyAutoMovedTags.find(areas.first) != RecentlyAutoMovedTags.end()) {
			double t = (double)clock() - RecentlyAutoMovedTags[areas.first] / ((double)CLOCKS_PER_SEC);
			if (t >= 0.8) {
				RecentlyAutoMovedTags.erase(areas.first);
			}
			else {
				continue;
			}
		}

		// We need to see wether the rotation will be clockwise or anti-clockwise
		bool isAntiClockwise = false;

		for (const auto area2 : tagAreas) {
			if (areas.first == area2.first)
				continue;

			if (IsTagBeingDragged(area2.first))
				continue;

			CRect h;
			if (h.IntersectRect(tagAreas[areas.first], area2.second)) {
				if (areas.second.left <= area2.second.left) {
					isAntiClockwise = true;
				}

				break;
			}
		}

		// We then rotate the tags until we did a 360 or there is no more conflicts

		POINT acPosPix = ConvertCoordFromPositionToPixel(GetPlugIn()->RadarTargetSelect(areas.first).GetPosition().GetPosition());
		int length = tagLineDefaultLength;
		if (TagLeaderLineLength.find(areas.first) != TagLeaderLineLength.end())
			length = TagLeaderLineLength[areas.first];

		int width = areas.second.Width();
		int height = areas.second.Height();

		for (double rotated = 0.0; abs(rotated) <= 360.0;) {
			// We first rotate the tag
			double newangle = fmod(TagAngles[areas.first] + rotated, 360.0f);

			POINT TagCenter;
			TagCenter.x = long(acPosPix.x + float(length * cos(DegToRad(newangle))));
			TagCenter.y = long(acPosPix.y + float(length * sin(DegToRad(newangle))));

			CRect NewRectangle(TagCenter.x - (width / 2), TagCenter.y - (height / 2), TagCenter.x + (width / 2), TagCenter.y + (height / 2));
			NewRectangle.NormalizeRect();

			// Assume there is no conflict, then try again

			bool isTagConflicing = false;

			for (const auto area2 : tagAreas) {
				if (areas.first == area2.first)
					continue;

				if (IsTagBeingDragged(area2.first))
					continue;

				CRect h;
				if (h.IntersectRect(NewRectangle, area2.second)) {
					isTagConflicing = true;
					break;
				}
			}

			if (!isTagConflicing) {
				TagAngles[areas.first] = fmod(TagAngles[areas.first] + rotated, 360);
				tagAreas[areas.first] = NewRectangle;
				RecentlyAutoMovedTags[areas.first] = clock();
				break;
			}

			if (isAntiClockwise)
				rotated -= 22.5f;
			else
				rotated += 22.5f;
		}
	}
}

// ReSharper restore CppMsExtAddressOfClassRValue

//---EuroScopePlugInExitCustom-----------------------------------------------

void CSMRRadar::EuroScopePlugInExitCustom()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())

		if (cursor != nullptr && cursor != NULL) {
			SetWindowLong(pluginWindow, GWL_WNDPROC, (LONG)gSourceProc);
		}
}