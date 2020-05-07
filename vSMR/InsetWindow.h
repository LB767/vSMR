#pragma once
#include "SMRRadar.hpp"
#include "EuroScopePlugIn.h"
#include <map>
#include <bstrlib\bstrwrap.h>
#include "Logger.h"

using namespace std;
using namespace EuroScopePlugIn;

class CInsetWindow
{
public:
	CInsetWindow(int Id);
	virtual ~CInsetWindow();

	// Definition
	int m_Id = -1, m_Scale = 15, m_Filter = 5500;
	RECT m_Area = { 200, 200, 600, 500 };
	POINT m_Offset = { 0, 0 }, m_OffsetInit = { 0, 0 }, m_OffsetDrag = { 0, 0 };
	bool m_Grip = false;
	double m_Rotation = 0;
	int m_ExtendedLinesLength = 15;
	int m_ExtendedLinesTickSpacing = 1;

	map<string, double> m_TagAngles;

	virtual void render(HDC Hdc, CSMRRadar * radar_screen, Graphics* gdi, POINT mouseLocation, multimap<CBString, CBString> DistanceTools);
	virtual void setAirport(CBString icao);
	virtual POINT projectPoint(CPosition pos);
	virtual void OnClickScreenObject(const char * sItemString, POINT Pt, int Button);
	virtual bool OnMoveScreenObject(const char * sObjectId, POINT Pt, RECT Area, bool released);
	
private:
	CBString icao;
	map<CBString, CPosition> AptPositions;
};

