#include "BookingInfo.h"
#include <algorithm>
#include <vector>

const long long int GUILD_ID = 635182631754924032;

/// <summary>
/// Constructor
/// </summary>
BookingInfo::BookingInfo()
{
}

/// <summary>
/// Constructor
/// </summary>
BookingInfo::BookingInfo(std::string &p_system)
{
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
/// Constructor with strings
/// </summary>
BookingInfo::BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system, dpp::cluster &p_bot) : BookingInfo(p_system)
{
    _user1String = p_user1;
    _user2String = p_user2;
}

/// <summary>
/// Constructor with strings
/// </summary>
BookingInfo::BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system, dpp::cluster &p_bot, dpp::user &p_creator) : BookingInfo(p_system)
{
    _user1String = p_user1;
    _user2String = p_user2;
    _creator = p_creator;
}

//Could do another constructor with the guild_members set

/// <summary>
/// Destructor
/// </summary>
BookingInfo::~BookingInfo()
{
}

//Only ever need to check first user to assume table is booked
bool BookingInfo::isBooked()
{
    if(_user1String.empty())
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

bool BookingInfo::isOwner(dpp::user p_owner)
{
    if((_user1Member != nullptr && _user1Member->get_user() !=  nullptr && p_owner == *_user1Member->get_user())
    || (_user2Member != nullptr && _user1Member->get_user() != nullptr && p_owner == *_user2Member->get_user()))
    {
        return true;
    }
    return false;
}

void BookingInfo::clearBooking()
{
    _user1String = "";
    _user2String = "";
    _user1Member.reset();
    _user2Member.reset();
    _system = "";
}

//Formats a string that is the table booking format
//Format is:
//  User1 vs User2
//  Table p_tableNum
//  System
std::string BookingInfo::formatMsg(int p_tableNum)
{
    std::string user1 = _user1Member != nullptr ? _user1Member->get_mention() : _user1String;
    std::string user2 = _user2Member != nullptr ? _user2Member->get_mention() : _user2String;
    if (_user1Member == nullptr)
    {
        printf("user1 null");
    }
    if (_user2Member == nullptr)
    {
        printf("user2 null");
    }
    std::string msg = user1 + " vs " + user2 + "\n"
                      + "Table " + std::to_string(p_tableNum) + "\n"
                      + _system;
    std::string creator;
    if(&_creator != nullptr)
    {
        msg.append("\n Booked by: " + _creator.username);
    }
    
    return msg;
}