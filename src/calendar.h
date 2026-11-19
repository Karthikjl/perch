// Calendar (a Google Calendar iCal link): downloads an iCal calendar and turns its events into
// one entry per day, repeating events included. No drawing here; notch.cpp shows them.
#pragma once
#include <string>
#include <vector>

struct CalEvent {
    int day;          // Cal_DayNumber of the day it is on
    int start, end;   // minutes since midnight, local time; -1 for all-day events
    std::wstring title;
};

// Days counted from 1970-01-01 (any fixed origin works; only differences matter).
int Cal_DayNumber(int y, int m, int d);
void Cal_Date(int day, int& y, int& m, int& d);
int Cal_Weekday(int day); // 0 = Sunday
int Cal_Today();

// Reads iCalendar text. Returns false when it isn't one (no BEGIN:VCALENDAR). Events, and each
// repeat of a repeating event, that fall on days fromDay..toDay go to `out`, sorted by day and time.
bool Cal_Parse(const std::string& ics, int fromDay, int toDay, std::vector<CalEvent>& out);

// Downloads `url` (https, http or webcal). 0 = ok, 1 = couldn't reach it (network, HTTP error).
int Cal_Download(const std::wstring& url, std::string& body);
