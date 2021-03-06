#include "stdafx.h"
#include "InsetWindow.hpp"


CInsetWindow::CInsetWindow(int Id)
{
	m_Id = Id;
}

CInsetWindow::~CInsetWindow()
{
}


void CInsetWindow::OnClickScreenObject(const char * sItemString, POINT Pt, int Button)
{
	if (Button == EuroScopePlugIn::BUTTON_RIGHT)
	{
		if (m_TagAngles.find(sItemString) != m_TagAngles.end())
		{
			m_TagAngles[sItemString] = fmod(m_TagAngles[sItemString] + 45.0, 360.0);
		} else
		{
			m_TagAngles[sItemString] = 45.0;
		}
	}

	if (Button == EuroScopePlugIn::BUTTON_LEFT)
	{
		if (m_TagAngles.find(sItemString) != m_TagAngles.end())
		{
			m_TagAngles[sItemString] = fmod(m_TagAngles[sItemString] - 45.0, 360.0);
		}
		else
		{
			m_TagAngles[sItemString] = 45.0;
		}
	}
}

bool CInsetWindow::OnMoveScreenObject(const char * sObjectId, POINT Pt, RECT Area, bool Released)
{
	if (strcmp(sObjectId, "window") == 0) {
		if (!this->m_Grip)
		{
			m_OffsetInit = m_Offset;
			m_OffsetDrag = Pt;
			m_Grip = true;
		}

		POINT maxoffset = { (m_Area.right - m_Area.left) / 2, (m_Area.bottom - (m_Area.top + 15)) / 2 };
		m_Offset.x = max(-maxoffset.x, min(maxoffset.x, m_OffsetInit.x + (Pt.x - m_OffsetDrag.x)));
		m_Offset.y = max(-maxoffset.y, min(maxoffset.y, m_OffsetInit.y + (Pt.y - m_OffsetDrag.y)));

		if (Released)
		{
			m_Grip = false;
		}
	}
	if (strcmp(sObjectId, "resize") == 0) {
		POINT TopLeft = { m_Area.left, m_Area.top };
		POINT BottomRight = { Area.right, Area.bottom };

		CRect newSize(TopLeft, BottomRight);
		newSize.NormalizeRect();

		if (newSize.Height() < 100) {
			newSize.top = m_Area.top;
			newSize.bottom = m_Area.bottom;
		}

		if (newSize.Width() < 300) {
			newSize.left = m_Area.left;
			newSize.right = m_Area.right;
		}

		m_Area = newSize;

		return Released;
	}
	if (strcmp(sObjectId, "topbar") == 0) {

		CRect appWindowRect(m_Area);
		appWindowRect.NormalizeRect();

		POINT TopLeft = { Area.left, Area.bottom + 1 };
		POINT BottomRight = { TopLeft.x + appWindowRect.Width(), TopLeft.y + appWindowRect.Height() };
		CRect newPos(TopLeft, BottomRight);
		newPos.NormalizeRect();

		m_Area = newPos;

		return Released;
	}

	return true;
}

bool CInsetWindow::ShouldDrawInWindow(CSMRRadar* radar_screen, CRadarTarget* rt) {

	auto refPos = radar_screen->AirportPositions[radar_screen->ActiveAirport];

	if (rt->GetGS() < 60 ||
		rt->GetPosition().GetPressureAltitude() > m_AltFilter ||
		rt->GetPosition().GetPressureAltitude() < radar_screen->CurrentConfig->getActiveProfile()["labels"]["airborne_altitude"].GetInt() ||
		refPos.DistanceTo(rt->GetPosition().GetPosition()) > m_RadarRange) {
		return false;
	}

	auto acPosPix = projectPoint(rt->GetPosition().GetPosition(), refPos);
	CRect windowAreaCRect(m_Area);
	vector<POINT> appAreaVect = { windowAreaCRect.TopLeft(),{ windowAreaCRect.right, windowAreaCRect.top }, windowAreaCRect.BottomRight(),{ windowAreaCRect.left, windowAreaCRect.bottom } };
	if (!Is_Inside(acPosPix, appAreaVect)) {
		return false;
	}

	return true;
}

POINT CInsetWindow::projectPoint(CPosition pos, CPosition ref)
{
	CRect areaRect(m_Area);
	areaRect.NormalizeRect();

	POINT refPt = areaRect.CenterPoint();
	refPt.x += m_Offset.x;
	refPt.y += m_Offset.y;

	POINT out = {0, 0};

	double dist = ref.DistanceTo(pos);
	double dir = TrueBearing(ref, pos);

	out.x = refPt.x + int(m_Zoom * dist * sin(dir) + 0.5);
	out.y = refPt.y - int(m_Zoom * dist * cos(dir) + 0.5);

	if (m_Rotation != 0)
	{
		return rotate_point(out, m_Rotation, refPt);
	} else
	{
		return out;
	}
}

RECT CInsetWindow::DrawToolbarButton(CDC * dc, const char* letter, CRect TopBar, int left, POINT mouseLocation)
{
	POINT TopLeft = { TopBar.right - left, TopBar.top + 2 };
	POINT BottomRight = { TopBar.right - (left - 11), TopBar.bottom - 2 };
	CRect Rect(TopLeft, BottomRight);
	Rect.NormalizeRect();
	CBrush ButtonBrush(RGB(60, 60, 60));
	dc->FillRect(Rect, &ButtonBrush);
	dc->SetTextColor(RGB(0, 0, 0));
	dc->TextOutA(Rect.left + 2, Rect.top, letter);

	if (mouseWithin(mouseLocation, Rect))
		dc->Draw3dRect(Rect, RGB(45, 45, 45), RGB(75, 75, 75));
	else
		dc->Draw3dRect(Rect, RGB(75, 75, 75), RGB(45, 45, 45));

	return Rect;
}

void CInsetWindow::render(HDC hDC, CSMRRadar * radar_screen, Graphics* gdi, POINT mouseLocation, multimap<CBString, CBString> DistanceTools)
{
	Logger::info("InsetWindow Render");
	CDC dc;
	dc.Attach(hDC);

	if (this->m_Id == -1)
		return;

	auto icao = radar_screen->ActiveAirport;
	auto AptPositions = radar_screen->AirportPositions;
	CPosition refPos = AptPositions[icao];

	COLORREF qBackgroundColor = radar_screen->CurrentConfig->getConfigColorRef(radar_screen->CurrentConfig->getActiveProfile()["approach_insets"]["background_color"]);
	CRect windowAreaCRect(m_Area);
	windowAreaCRect.NormalizeRect();

	// We create the radar
	dc.FillSolidRect(windowAreaCRect, qBackgroundColor);
	radar_screen->AddScreenObject(m_Id, "window", m_Area, true, "");

	POINT refPt = windowAreaCRect.CenterPoint();
	refPt.x += m_Offset.x;
	refPt.y += m_Offset.y;

	Logger::info("InsetWindow 190");
	// Here we draw all runways for the airport
	for (CSectorElement rwy = radar_screen->GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY);
		rwy.IsValid();
		rwy = radar_screen->GetPlugIn()->SectorFileElementSelectNext(rwy, SECTOR_ELEMENT_RUNWAY))
	{

		Logger::info("InsetWindow 197");
		if (StartsWith(icao, rwy.GetAirportName()))
		{
			CPen RunwayPen(PS_SOLID, 1, radar_screen->CurrentConfig->getConfigColorRef(radar_screen->CurrentConfig->getActiveProfile()["approach_insets"]["runway_color"]));
			CPen ExtendedCentreLinePen(PS_SOLID, 1, radar_screen->CurrentConfig->getConfigColorRef(radar_screen->CurrentConfig->getActiveProfile()["approach_insets"]["extended_lines_color"]));
			CPen* oldPen = dc.SelectObject(&RunwayPen);

			CPosition EndOne, EndTwo;
			rwy.GetPosition(&EndOne, 0);
			rwy.GetPosition(&EndTwo, 1);

			POINT Pt1, Pt2;
			Pt1 = projectPoint(EndOne, refPos);
			Pt2 = projectPoint(EndTwo, refPos);

			POINT toDraw1, toDraw2;
			if (LiangBarsky(m_Area, Pt1, Pt2, toDraw1, toDraw2)) {
				dc.MoveTo(toDraw1);
				dc.LineTo(toDraw2);

			}

			Logger::info("InsetWindow 219");
			if (rwy.IsElementActive(false, 0) || rwy.IsElementActive(false, 1))
			{
				CPosition Threshold, OtherEnd;
				if (rwy.IsElementActive(false, 0))
				{
					Threshold = EndOne; 
					OtherEnd = EndTwo;
				} else
				{
					Threshold = EndTwo; 
					OtherEnd = EndOne;
				}
					

				double reverseHeading = RadToDeg(TrueBearing(OtherEnd, Threshold));
				double length = m_ExtendedLinesLength * 1852.0;

				// Drawing the extended centreline
				CPosition endExtended = BetterHarversine(Threshold, reverseHeading, length);

				Pt1 = projectPoint(Threshold, refPos);
				Pt2 = projectPoint(endExtended, refPos);

				if (LiangBarsky(m_Area, Pt1, Pt2, toDraw1, toDraw2)) {
					dc.SelectObject(&ExtendedCentreLinePen);
					dc.MoveTo(toDraw1);
					dc.LineTo(toDraw2);
				}

				// Drawing the ticks
				int increment = m_ExtendedLinesTickSpacing * 1852;
				if (increment > 0){
					for (int j = increment; j <= int(m_ExtendedLinesLength * 1852.0); j += increment) {

						CPosition tickPosition = BetterHarversine(Threshold, reverseHeading, j);
						CPosition tickBottom = BetterHarversine(tickPosition, fmod(reverseHeading - 90, 360), 500);
						CPosition tickTop = BetterHarversine(tickPosition, fmod(reverseHeading + 90, 360), 500);

						Pt1 = projectPoint(tickBottom, refPos);
						Pt2 = projectPoint(tickTop, refPos);

						if (LiangBarsky(m_Area, Pt1, Pt2, toDraw1, toDraw2)) {
							dc.SelectObject(&ExtendedCentreLinePen);
							dc.MoveTo(toDraw1);
							dc.LineTo(toDraw2);
						}
					}
				}
			} 

			dc.SelectObject(&oldPen);
		}
	}

	// Aircrafts
	Logger::info("InsetWindow 274");
	radar_screen->DrawTargets(gdi, &dc, this);
	radar_screen->DrawTags(gdi, this);
	Logger::info("InsetWindow 277");

	/*
	CRadarTarget rt;
	for (rt = radar_screen->GetPlugIn()->RadarTargetSelectFirst();
		rt.IsValid();
		rt = radar_screen->GetPlugIn()->RadarTargetSelectNext(rt))
	{
		int radarRange = radar_screen->CurrentConfig->getActiveProfile()["filters"]["radar_range_nm"].GetInt();

		if (rt.GetGS() < 60 ||
			rt.GetPosition().GetPressureAltitude() > m_Filter ||
			!rt.IsValid() ||
			!rt.GetPosition().IsValid() ||
			rt.GetPosition().GetPosition().DistanceTo(AptPositions[icao]) > radarRange)
			continue;

		CPosition RtPos2 = rt.GetPosition().GetPosition();
		CRadarTargetPositionData RtPos = rt.GetPosition();
		auto fp = radar_screen->GetPlugIn()->FlightPlanSelect(rt.GetCallsign());
		auto reportedGs = RtPos.GetReportedGS();

		auto RtPoint = projectPoint(RtPos2, refPos);

		int lenght = 50;

		POINT TagCenter;
		if (m_TagAngles.find(rt.GetCallsign()) == m_TagAngles.end())
		{
			m_TagAngles[rt.GetCallsign()] = 45.0; // TODO: Not the best, ah well
		}

		TagCenter.x = long(RtPoint.x + float(lenght * cos(DegToRad(m_TagAngles[rt.GetCallsign()]))));
		TagCenter.y = long(RtPoint.y + float(lenght * sin(DegToRad(m_TagAngles[rt.GetCallsign()]))));
		// Drawing the tags, what a mess

		// ----- Generating the replacing map -----
		map<CBString, CSMRRadar::TagItem> TagMap = CSMRRadar::GenerateTagData(rt, fp, radar_screen, icao);

		//
		// ----- Now the hard part, drawing (using gdi+) -------
		//	

		CSMRRadar::TagTypes TagType = CSMRRadar::TagTypes::Departure;
		CSMRRadar::TagTypes ColorTagType = CSMRRadar::TagTypes::Departure;

		if (fp.IsValid() && strcmp(fp.GetFlightPlanData().GetDestination(), radar_screen->ActiveAirport) == 0) {
				TagType = CSMRRadar::TagTypes::Arrival;
				ColorTagType = CSMRRadar::TagTypes::Arrival;
		}

		if (rt.GetPosition().GetPressureAltitude() > radar_screen->CurrentConfig->getActiveProfile()["labels"]["airborne_altitude"].GetInt()) {
			TagType = CSMRRadar::TagTypes::Airborne;

			// Is "use_departure_arrival_coloring" enabled? if not, then use the airborne colors
			bool useDepArrColors = radar_screen->CurrentConfig->getActiveProfile()["labels"]["airborne"]["use_departure_arrival_coloring"].GetBool();
			if (!useDepArrColors) {
				ColorTagType = CSMRRadar::TagTypes::Airborne;
			}
		}

		bool AcisCorrelated = radar_screen->IsCorrelated(radar_screen->GetPlugIn()->FlightPlanSelect(rt.GetCallsign()), rt);
		if (!AcisCorrelated && reportedGs >= 3)
		{
			TagType = CSMRRadar::TagTypes::Uncorrelated;
			ColorTagType = CSMRRadar::TagTypes::Uncorrelated;
		}

		// Get the TAG label settings
		const Value& LabelsSettings = radar_screen->CurrentConfig->getActiveProfile()["labels"];

		// First we need to figure out the tag size
		Gdiplus::REAL TagWidth = 0, TagHeight = 0;
		RectF mesureRect;
		gdi->MeasureString(L" ", wcslen(L" "), radar_screen->customFont, PointF(0, 0), &Gdiplus::StringFormat(), &mesureRect);
		auto blankWidth = mesureRect.GetRight();

		// default font size
		mesureRect = RectF(0, 0, 0, 0);
		gdi->MeasureString(L"AZERTYUIOPQSDFGHJKLMWXCVBN", wcslen(L"AZERTYUIOPQSDFGHJKLMWXCVBN"), radar_screen->customFont, PointF(0, 0), &Gdiplus::StringFormat(), &mesureRect);
		auto oneLineHeight = mesureRect.GetBottom();

		// bigger font size if used for 1st TAG line		
		CBString font_name = radar_screen->CurrentConfig->getActiveProfile()["font"]["font_name"].GetString();
		wstring wide_font_name = ToWString(font_name);

		float fontsize = radar_screen->customFont->GetSize();
		double fontSizeScaling = 1.0;
		if (LabelsSettings[Utils::getEnumString(ColorTagType)].HasMember("first_line_font_factor")) {
			fontSizeScaling = LabelsSettings[Utils::getEnumString(ColorTagType)]["first_line_font_factor"].GetDouble();
			fontsize = round((float)fontSizeScaling * fontsize);
		}
		Gdiplus::Font* firstLineFont = new Gdiplus::Font(wide_font_name.c_str(), Gdiplus::REAL(fontsize), radar_screen->customFont->GetStyle(), Gdiplus::UnitPixel); ;

		mesureRect = RectF(0, 0, 0, 0);
		gdi->MeasureString(L"AZERTYUIOPQSDFGHJKLMWXCVBN", wcslen(L"AZERTYUIOPQSDFGHJKLMWXCVBN"),
			firstLineFont, PointF(0, 0), &Gdiplus::StringFormat(), &mesureRect);
		auto firstLineHeight = mesureRect.GetBottom();

		// get label lines definitions
		const Value& LabelLines = LabelsSettings[Utils::getEnumString(TagType)]["definition"];
		vector<vector<CSMRRadar::TagItem>> ReplacedLabelLines;

		if (!LabelLines.IsArray())
			return;

		for (unsigned int i = 0; i < LabelLines.Size(); i++)
		{
			const Value& line = LabelLines[i];
			vector<CSMRRadar::TagItem> lineTagItemArray;

			// Adds one line height
			if (i == 0) {
				TagHeight += firstLineHeight; // special case 1st line
			}
			else {
				TagHeight += oneLineHeight;
			}

			Gdiplus::REAL TempTagWidth = 0;

			for (unsigned int j = 0; j < line.Size(); j++)
			{
				mesureRect = RectF(0, 0, 0, 0);
				CBString tagKey = line[j].GetString();

				//for (auto& kv : TagReplacingMap)
				//replaceAll(element, kv.first, kv.second);

				lineTagItemArray.push_back(TagMap[tagKey]);

				wstring wstr = ToWString(TagMap[tagKey].value);
				if (i == 0) {
					gdi->MeasureString(wstr.c_str(), wcslen(wstr.c_str()), firstLineFont, PointF(0, 0), &Gdiplus::StringFormat(), &mesureRect); // special case for first line
				}
				else {
					gdi->MeasureString(wstr.c_str(), wcslen(wstr.c_str()), radar_screen->customFont, PointF(0, 0), &Gdiplus::StringFormat(), &mesureRect);
				}
				TempTagWidth += mesureRect.GetRight();

				if (j != line.Size() - 1)
					TempTagWidth += blankWidth;
			}

			TagWidth = max(TagWidth, TempTagWidth);

			ReplacedLabelLines.push_back(lineTagItemArray);
		}

		// Pfiou, done with that, now we can draw the actual rectangle.

		// We need to figure out if the tag color changes according to RIMCAS alerts, or not
		bool rimcasLabelOnly = radar_screen->CurrentConfig->getActiveProfile()["rimcas"]["rimcas_label_only"].GetBool();

		Color definedBackgroundColor = radar_screen->CurrentConfig->getConfigColor(LabelsSettings[Utils::getEnumString(ColorTagType)]["background_color"]);
		if (TagType == CSMRRadar::TagTypes::Departure) {
			if (TagMap["sid"].value != "" && radar_screen->CurrentConfig->isSidColorAvail(TagMap["sid"].value, radar_screen->ActiveAirport)) {
				definedBackgroundColor = radar_screen->CurrentConfig->getSidColor(TagMap["sid"].value, radar_screen->ActiveAirport);
			}

			if (fp.GetFlightPlanData().GetPlanType() == "I" && TagMap["asid"].value == "" && LabelsSettings[Utils::getEnumString(ColorTagType)].HasMember("nosid_color")) {
				definedBackgroundColor = radar_screen->CurrentConfig->getConfigColor(LabelsSettings[Utils::getEnumString(ColorTagType)]["nosid_color"]);
			}

			if (TagMap["actype"].value == ACT_TYPE_EMPTY_SPACES && LabelsSettings[Utils::getEnumString(ColorTagType)].HasMember("nofpl_color")) {
				definedBackgroundColor = radar_screen->CurrentConfig->getConfigColor(LabelsSettings[Utils::getEnumString(ColorTagType)]["nofpl_color"]);
			}
		}

		Color TagBackgroundColor = radar_screen->RimcasInstance->GetAircraftColor(rt.GetCallsign(),
			definedBackgroundColor,
			radar_screen->CurrentConfig->getConfigColor(LabelsSettings[Utils::getEnumString(ColorTagType)]["background_color_on_runway"]),
			radar_screen->CurrentConfig->getConfigColor(radar_screen->CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_one"]),
			radar_screen->CurrentConfig->getConfigColor(radar_screen->CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_two"]));

		if (rimcasLabelOnly)
			TagBackgroundColor = radar_screen->RimcasInstance->GetAircraftColor(rt.GetCallsign(),
				definedBackgroundColor,
				radar_screen->CurrentConfig->getConfigColor(LabelsSettings[Utils::getEnumString(ColorTagType)]["background_color_on_runway"]));

		CRect TagBackgroundRect((int)(TagCenter.x - (TagWidth / 2.0)), (int)(TagCenter.y - (TagHeight / 2.0)), (int)(TagCenter.x + (TagWidth / 2.0)), (int)(TagCenter.y + (TagHeight / 2.0)));

		if (Is_Inside(TagBackgroundRect.TopLeft(), appAreaVect) &&
			Is_Inside(RtPoint, appAreaVect) &&
			Is_Inside(TagBackgroundRect.BottomRight(), appAreaVect)) {

			SolidBrush TagBackgroundBrush(TagBackgroundColor);
			gdi->FillRectangle(&TagBackgroundBrush, CopyRect(TagBackgroundRect));

			SolidBrush FontColor(radar_screen->ColorManager->get_corrected_color("label",
				radar_screen->CurrentConfig->getConfigColor(LabelsSettings[Utils::getEnumString(ColorTagType)]["text_color"])));
			SolidBrush SquawkErrorColor(radar_screen->ColorManager->get_corrected_color("label",
				radar_screen->CurrentConfig->getConfigColor(LabelsSettings["squawk_error_color"])));
			SolidBrush RimcasTextColor(radar_screen->CurrentConfig->getConfigColor(radar_screen->CurrentConfig->getActiveProfile()["rimcas"]["alert_text_color"]));

			Gdiplus::REAL heightOffset = 0;
			for (auto&& line : ReplacedLabelLines)
			{
				Gdiplus::REAL widthOffset = 0;
				for (auto&& tagItem : line)
				{
					SolidBrush* color = &FontColor;
					if (TagMap["sqerror"].value.length() > 0 && tagItem.value == TagMap["sqerror"].value)
						color = &SquawkErrorColor;

					if (radar_screen->RimcasInstance->getAlert(rt.GetCallsign()) != CRimcas::NoAlert)
						color = &RimcasTextColor;

					RectF mRect(0, 0, 0, 0);
					wstring welement = ToWString(tagItem.value);

					if (heightOffset == 0) { // first line
						gdi->DrawString(welement.c_str(), wcslen(welement.c_str()), firstLineFont,
							PointF(Gdiplus::REAL(TagBackgroundRect.left) + widthOffset, Gdiplus::REAL(TagBackgroundRect.top) + heightOffset),
							&Gdiplus::StringFormat(), color);

						gdi->MeasureString(welement.c_str(), wcslen(welement.c_str()), firstLineFont,
							PointF(0, 0), &Gdiplus::StringFormat(), &mRect);
					}
					else {
						gdi->DrawString(welement.c_str(), wcslen(welement.c_str()), radar_screen->customFont,
							PointF(Gdiplus::REAL(TagBackgroundRect.left) + widthOffset, Gdiplus::REAL(TagBackgroundRect.top) + heightOffset),
							&Gdiplus::StringFormat(), color);

						gdi->MeasureString(welement.c_str(), wcslen(welement.c_str()), radar_screen->customFont,
							PointF(0, 0), &Gdiplus::StringFormat(), &mRect);
					}

					CRect ItemRect((int)(TagBackgroundRect.left + widthOffset), (int)(TagBackgroundRect.top + heightOffset),
						(int)(TagBackgroundRect.left + widthOffset + mRect.GetRight()), (int)(TagBackgroundRect.top + heightOffset + mRect.GetBottom()));

					radar_screen->AddScreenObject(tagItem.function, rt.GetCallsign(), ItemRect, false, radar_screen->GetBottomLine(rt.GetCallsign()));

					widthOffset += mRect.GetRight();
					widthOffset += blankWidth;
				}

				heightOffset += oneLineHeight;
			}

			// Drawing the leader line
			RECT TagBackRectData = TagBackgroundRect;
			POINT toDraw1, toDraw2;
			if (LiangBarsky(TagBackRectData, RtPoint, TagBackgroundRect.CenterPoint(), toDraw1, toDraw2))
				gdi->DrawLine(&Pen(radar_screen->ColorManager->get_corrected_color("symbol", Color::White)), PointF(Gdiplus::REAL(RtPoint.x), Gdiplus::REAL(RtPoint.y)), PointF(Gdiplus::REAL(toDraw1.x), Gdiplus::REAL(toDraw1.y)));

			// If we use a RIMCAS label only, we display it, and adapt the rectangle
			CRect oldCrectSave = TagBackgroundRect;

			if (rimcasLabelOnly) {
				Color RimcasLabelColor = radar_screen->RimcasInstance->GetAircraftColor(rt.GetCallsign(), Color::AliceBlue, Color::AliceBlue,
					radar_screen->CurrentConfig->getConfigColor(radar_screen->CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_one"]),
					radar_screen->CurrentConfig->getConfigColor(radar_screen->CurrentConfig->getActiveProfile()["rimcas"]["background_color_stage_two"]));

				if (RimcasLabelColor.ToCOLORREF() != Color(Color::AliceBlue).ToCOLORREF()) {
					int rimcas_height = 0;

					wstring rimcasw = L"ALERT";
					RectF RectRimcas_height;

					gdi->MeasureString(rimcasw.c_str(), wcslen(rimcasw.c_str()), radar_screen->customFont, PointF(0, 0), &Gdiplus::StringFormat(), &RectRimcas_height);
					rimcas_height = int(RectRimcas_height.GetBottom());

					// Drawing the rectangle

					CRect RimcasLabelRect(TagBackgroundRect.left, TagBackgroundRect.top - rimcas_height, TagBackgroundRect.right, TagBackgroundRect.top);
					gdi->FillRectangle(&SolidBrush(RimcasLabelColor), CopyRect(RimcasLabelRect));
					TagBackgroundRect.top -= rimcas_height;

					// Drawing the text

					StringFormat stformat = new StringFormat();
					stformat.SetAlignment(StringAlignment::StringAlignmentCenter);
					gdi->DrawString(rimcasw.c_str(), wcslen(rimcasw.c_str()), radar_screen->customFont, PointF(Gdiplus::REAL((TagBackgroundRect.left + TagBackgroundRect.right) / 2), Gdiplus::REAL(TagBackgroundRect.top)), &stformat, &RimcasTextColor);

				}
			}

			// Filtering the targets
			POINT RtPoint, hPoint;

			RtPoint = projectPoint(RtPos2, refPos);

			CRadarTargetPositionData hPos = rt.GetPreviousPosition(rt.GetPosition());
			for (int i = 1; i < radar_screen->Trail_App; i++) {
				if (!hPos.IsValid())
					continue;

				hPoint = projectPoint(hPos.GetPosition(), refPos);

				if (Is_Inside(hPoint, appAreaVect)) {
					dc.SetPixel(hPoint, radar_screen->ColorManager->get_corrected_color("symbol", Color::White).ToCOLORREF());
				}

				hPos = rt.GetPreviousPosition(hPos);
			}

			if (Is_Inside(RtPoint, appAreaVect)) {
				dc.SelectObject(&WhitePen);

				if (RtPos.GetTransponderC()) {
					dc.MoveTo({ RtPoint.x, RtPoint.y - 4 });
					dc.LineTo({ RtPoint.x - 4, RtPoint.y });
					dc.LineTo({ RtPoint.x, RtPoint.y + 4 });
					dc.LineTo({ RtPoint.x + 4, RtPoint.y });
					dc.LineTo({ RtPoint.x, RtPoint.y - 4 });
				}
				else {
					dc.MoveTo(RtPoint.x, RtPoint.y);
					dc.LineTo(RtPoint.x - 4, RtPoint.y - 4);
					dc.MoveTo(RtPoint.x, RtPoint.y);
					dc.LineTo(RtPoint.x + 4, RtPoint.y - 4);
					dc.MoveTo(RtPoint.x, RtPoint.y);
					dc.LineTo(RtPoint.x - 4, RtPoint.y + 4);
					dc.MoveTo(RtPoint.x, RtPoint.y);
					dc.LineTo(RtPoint.x + 4, RtPoint.y + 4);
				}

				CRect TargetArea(RtPoint.x - 4, RtPoint.y - 4, RtPoint.x + 4, RtPoint.y + 4);
				TargetArea.NormalizeRect();
				radar_screen->AddScreenObject(DRAWING_AC_SYMBOL_APPWINDOW_BASE + (m_Id - APPWINDOW_BASE), rt.GetCallsign(), TargetArea, false, radar_screen->GetBottomLine(rt.GetCallsign()));
			}

			// Predicted Track Line
			// It starts 10 seconds away from the ac
			double d = double(rt.GetPosition().GetReportedGS()*0.514444) * 10;
			CPosition AwayBase = BetterHarversine(rt.GetPosition().GetPosition(), rt.GetTrackHeading(), d);

			d = double(rt.GetPosition().GetReportedGS()*0.514444) * (radar_screen->PredictedLenght * 60) - 10;
			CPosition PredictedEnd = BetterHarversine(AwayBase, rt.GetTrackHeading(), d);

			POINT liangOne, liangTwo;

			if (LiangBarsky(m_Area, projectPoint(AwayBase, refPos), projectPoint(PredictedEnd, refPos), liangOne, liangTwo)) {
				dc.SelectObject(&WhitePen);
				dc.MoveTo(liangOne);
				dc.LineTo(liangTwo);
			}

			if (mouseWithin(mouseLocation, { RtPoint.x - 4, RtPoint.y - 4, RtPoint.x + 4, RtPoint.y + 4 })) {
				dc.MoveTo(RtPoint.x, RtPoint.y - 6);
				dc.LineTo(RtPoint.x - 4, RtPoint.y - 10);
				dc.MoveTo(RtPoint.x, RtPoint.y - 6);
				dc.LineTo(RtPoint.x + 4, RtPoint.y - 10);

				dc.MoveTo(RtPoint.x, RtPoint.y + 6);
				dc.LineTo(RtPoint.x - 4, RtPoint.y + 10);
				dc.MoveTo(RtPoint.x, RtPoint.y + 6);
				dc.LineTo(RtPoint.x + 4, RtPoint.y + 10);

				dc.MoveTo(RtPoint.x - 6, RtPoint.y);
				dc.LineTo(RtPoint.x - 10, RtPoint.y - 4);
				dc.MoveTo(RtPoint.x - 6, RtPoint.y);
				dc.LineTo(RtPoint.x - 10, RtPoint.y + 4);

				dc.MoveTo(RtPoint.x + 6, RtPoint.y);
				dc.LineTo(RtPoint.x + 10, RtPoint.y - 4);
				dc.MoveTo(RtPoint.x + 6, RtPoint.y);
				dc.LineTo(RtPoint.x + 10, RtPoint.y + 4);
			}



			// Adding the tag screen object

			//radar_screen->AddScreenObject(DRAWING_TAG, rt.GetCallsign(), TagBackgroundRect, true, GetBottomLine(rt.GetCallsign()).c_str());

			TagBackgroundRect = oldCrectSave;

			// Now adding the clickable zones
		}
	}
	*/


	//---------------------------------
	// Drawing distance tools
	//---------------------------------
    radar_screen->DrawDistanceTools(gdi, &dc, this);
	/*
	for (auto&& kv : DistanceTools)
	{
		CRadarTarget one = radar_screen->GetPlugIn()->RadarTargetSelect(kv.first);
		CRadarTarget two = radar_screen->GetPlugIn()->RadarTargetSelect(kv.second);

		int radarRange = radar_screen->CurrentConfig->getActiveProfile()["filters"]["radar_range_nm"].GetInt();

		if (one.GetGS() < 60 ||
			one.GetPosition().GetPressureAltitude() > m_Filter ||
			!one.IsValid() ||
			!one.GetPosition().IsValid() ||
			one.GetPosition().GetPosition().DistanceTo(AptPositions[icao]) > radarRange)
			continue;

		if (two.GetGS() < 60 ||
			two.GetPosition().GetPressureAltitude() > m_Filter ||
			!two.IsValid() ||
			!two.GetPosition().IsValid() ||
			two.GetPosition().GetPosition().DistanceTo(AptPositions[icao]) > radarRange)
			continue;

		CPen Pen(PS_SOLID, 1, RGB(255, 255, 255));
		CPen *oldPen = dc.SelectObject(&Pen);

		POINT onePoint = projectPoint(one.GetPosition().GetPosition(), refPos);
		POINT twoPoint = projectPoint(two.GetPosition().GetPosition(), refPos);

		POINT toDraw1, toDraw2;
		if (LiangBarsky(m_Area, onePoint, twoPoint, toDraw1, toDraw2)) {
			dc.MoveTo(toDraw1);
			dc.LineTo(toDraw2);
		}

		POINT TextPos = { twoPoint.x + 20, twoPoint.y };

		double Distance = one.GetPosition().GetPosition().DistanceTo(two.GetPosition().GetPosition());
		double Bearing = one.GetPosition().GetPosition().DirectionTo(two.GetPosition().GetPosition());

		char QDRText[32];
		sprintf_s(QDRText, "%.1f\xB0 / %.1fNM", Bearing, Distance);
		COLORREF old_color = dc.SetTextColor(RGB(0, 0, 0));

		CRect ClickableRect = { TextPos.x - 2, TextPos.y, TextPos.x + dc.GetTextExtent(QDRText).cx + 2, TextPos.y + dc.GetTextExtent(QDRText).cy };
		if (Is_Inside(ClickableRect.TopLeft(), appAreaVect) && Is_Inside(ClickableRect.BottomRight(), appAreaVect))
		{
			gdi->FillRectangle(&SolidBrush(Color(127, 122, 122)), CopyRect(ClickableRect));
			dc.Draw3dRect(ClickableRect, RGB(75, 75, 75), RGB(45, 45, 45));
			dc.TextOutA(TextPos.x, TextPos.y, QDRText);

			radar_screen->AddScreenObject(RIMCAS_DISTANCE_TOOL, kv.first + "," + kv.second, ClickableRect, false, "");
		}
		
		dc.SetTextColor(old_color);

		dc.SelectObject(oldPen);
	}
	*/

	// Resize square
	qBackgroundColor = RGB(60, 60, 60);
	POINT BottomRight = { m_Area.right, m_Area.bottom };
	POINT TopLeft = { BottomRight.x - 10, BottomRight.y - 10 };
	CRect ResizeArea = { TopLeft, BottomRight };
	ResizeArea.NormalizeRect();
	dc.FillSolidRect(ResizeArea, qBackgroundColor);
	radar_screen->AddScreenObject(m_Id, "resize", ResizeArea, true, "");

	dc.Draw3dRect(ResizeArea, RGB(0, 0, 0), RGB(0, 0, 0));

	// Sides
	//CBrush FrameBrush(RGB(35, 35, 35));
	CBrush FrameBrush(RGB(127, 122, 122));
	COLORREF TopBarTextColor(RGB(35, 35, 35));
	dc.FrameRect(windowAreaCRect, &FrameBrush);

	// Topbar
	TopLeft = windowAreaCRect.TopLeft();
	TopLeft.y = TopLeft.y - 15;
	BottomRight = { windowAreaCRect.right, windowAreaCRect.top };
	CRect TopBar(TopLeft, BottomRight);
	TopBar.NormalizeRect();
	dc.FillRect(TopBar, &FrameBrush);
	POINT TopLeftText = { TopBar.left + 5, TopBar.bottom - dc.GetTextExtent("SRW 1").cy };
	COLORREF oldTextColorC = dc.SetTextColor(TopBarTextColor);

	radar_screen->AddScreenObject(m_Id, "topbar", TopBar, true, "");

	CString Toptext;
	Toptext.Format("SRW %d", m_Id - SRW_APPWINDOW);
	dc.TextOutA(TopLeftText.x + (TopBar.right-TopBar.left) / 2 - dc.GetTextExtent("SRW 1").cx , TopLeftText.y, Toptext);

	// View button
	CRect RangeRect = DrawToolbarButton(&dc, "V", TopBar, 29, mouseLocation);
	radar_screen->AddScreenObject(m_Id, "view", RangeRect, false, "");

	// Filters button
	CRect FilterRect = DrawToolbarButton(&dc, "F", TopBar, 42, mouseLocation);
	radar_screen->AddScreenObject(m_Id, "filters", FilterRect, false, "");

	// Extended centerline button
	CRect CenterlineRect = DrawToolbarButton(&dc, "C", TopBar, 55, mouseLocation);
	radar_screen->AddScreenObject(m_Id, "centerline", CenterlineRect, false, "");

	// Predicted track line button
	CRect PredictedTrackLineRect = DrawToolbarButton(&dc, "P", TopBar, 68, mouseLocation);
	radar_screen->AddScreenObject(m_Id, "predictedtrackline", PredictedTrackLineRect, false, "");

	dc.SetTextColor(oldTextColorC);

	// Close
	POINT TopLeftClose = { TopBar.right - 16, TopBar.top + 2 };
	POINT BottomRightClose = { TopBar.right - 5, TopBar.bottom - 2 };
	CRect CloseRect(TopLeftClose, BottomRightClose);
	CloseRect.NormalizeRect();
	CBrush CloseBrush(RGB(60, 60, 60));
	dc.FillRect(CloseRect, &CloseBrush);
	CPen BlackPen(PS_SOLID, 1, RGB(0, 0, 0));
	dc.SelectObject(BlackPen);
	dc.MoveTo(CloseRect.TopLeft());
	dc.LineTo(CloseRect.BottomRight());
	dc.MoveTo({ CloseRect.right - 1, CloseRect.top });
	dc.LineTo({ CloseRect.left - 1, CloseRect.bottom });

	if (mouseWithin(mouseLocation, CloseRect))
		dc.Draw3dRect(CloseRect, RGB(45, 45, 45), RGB(75, 75, 75));
	else
		dc.Draw3dRect(CloseRect, RGB(75, 75, 75), RGB(45, 45, 45));

	radar_screen->AddScreenObject(m_Id, "close", CloseRect, false, "");

	dc.Detach();
}