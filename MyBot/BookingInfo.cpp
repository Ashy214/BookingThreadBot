#include "BookingInfo.h"
#include <algorithm>
#include <vector>
/// <summary>
/// Constructor
/// </summary>
BookingInfo::BookingInfo()
{
}
/// <summary>
/// Constructor
/// </summary>
BookingInfo::BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system)
{
    _user1 = p_user1;
    _user2 = p_user2;
    std::transform(p_system.begin(), p_system.end(), p_system.begin(), ::tolower);
    _system = p_system;
    tableSystem = std::make_shared<std::map<std::string, std::vector<int>>>();
    tableSystem->insert({ "40k", {2,3,4,5,6,7,8,9,10,11,13} });
    tableSystem->insert({ "aos", {2,3,4,5,6,7,9,10,11,13} });
    tableSystem->insert({ "heresy", {2,3,4,5,6,7,8,9,13} });
    tableSystem->insert({ "kill team", {8} });
    tableSystem->insert({ "necromunda", {8} });
}

/// <summary>
/// Destructor
/// </summary>
BookingInfo::~BookingInfo()
{
}

//Only ever need to check first user to assume table is booked
bool BookingInfo::isBooked()
{
    if(_user1.empty())
    {
        return false;
    }
    return true;
}

//Tells us if the table is suitable for game system being used
bool BookingInfo::isSuitable(int p_tableNum)
{
    bool suitable = false;
    auto it = tableSystem->find(_system);
    if(it != tableSystem->end())
    {
        if (std::find(it->second.begin(), it->second.end(), p_tableNum) != it->second.end())
        {
            suitable = true;
        }
    }
    else
    {
        //Game system doesn't match what we expect so might be something we've not included. Return it is suitable to not cause concern
        suitable = true;
    }
    return suitable;
}
