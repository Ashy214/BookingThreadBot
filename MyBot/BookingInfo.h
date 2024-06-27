#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <dpp/dpp.h>
class BookingInfo
{
    private:
        std::string _user1String;
        std::string _user2String;
        std::shared_ptr<dpp::guild_member> _user1Member;
        std::shared_ptr<dpp::guild_member> _user2Member;
        std::string _system;
        dpp::user _creator;
        std::shared_ptr<std::map<std::string, std::vector<int>>> tableSystem;

        /// <summary>
        /// Constructor. Private as we don't want bookingInfo obj created without users
        /// </summary>
        BookingInfo(std::string& p_system);

    public:
        /// <summary>
        /// Constructor
        /// </summary>
        BookingInfo();

        /// <summary>
        /// Constructor with strings
        /// </summary>
        BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system);

        /// <summary>
        /// Constructor with strings
        /// </summary>
        BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system, dpp::user &p_creator);

        /// <summary>
        /// Destructor
        /// </summary>
        ~BookingInfo();

        bool isBooked();
        bool isSuitable(int p_tableNum);
        bool isOwner(dpp::user p_owner);
        void clearBooking();
        std::string formatMsg(int p_tableNum);

        inline std::string getUser1String() { return _user1String; }
        inline std::string getUser2String() { return _user2String; }
        //inline dpp::guild_member getUser1Member() { return *_user1Member; }
        inline dpp::guild_member getUser2Member() { return *_user2Member; }
        inline std::string getSystem() { return _system; }
        inline dpp::user getCreator() { return _creator; }
        inline void set_user1(std::string& p_user1) { _user1String = p_user1; }
        inline void set_user2(std::string& p_user2) { _user1String = p_user2; }
        inline void set_system(std::string p_system) { _system = p_system; }
        inline void set_creator(dpp::user& p_user) { _creator = p_user; }
        inline void set_user1Member(dpp::guild_member &p_guildUser1) { _user1Member = std::make_shared<dpp::guild_member>(p_guildUser1); }
        inline void set_user2Member(dpp::guild_member &p_guildUser2) { _user2Member = std::make_shared<dpp::guild_member>(p_guildUser2); }
};

