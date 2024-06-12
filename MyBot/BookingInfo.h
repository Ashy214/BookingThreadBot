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
        std::shared_ptr<dpp::guild_member> _user1Member;// = std::shared_ptr<dpp::guild_member>(new dpp::guild_member);
        std::shared_ptr<dpp::guild_member> _user2Member;
        std::string _system;
        std::shared_ptr<std::map<std::string, std::vector<int>>> tableSystem;
        bool _callback1;
        bool _callback2;

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
        BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system, dpp::cluster &p_bot);

        /// <summary>
        /// Destructor
        /// </summary>
        ~BookingInfo();

        bool isBooked();
        bool isSuitable(int p_tableNum);
        void findUser1(dpp::cluster &p_bot);
        std::string formatMsg(int p_tableNum);

        inline std::string getUser1String() { return _user1String; }
        inline std::string getUser2String() { return _user2String; }
        //inline dpp::guild_member getUser1Member() { return *_user1Member; }
        inline dpp::guild_member getUser2Member() { return *_user2Member; }
        inline std::string getSystem() { return _system; }
        inline void set_user1Member(dpp::guild_member p_guildUser1) { _user1Member = std::make_shared<dpp::guild_member>(p_guildUser1); }
        inline void set_user2Member(dpp::guild_member p_guildUser2) { _user2Member = std::make_shared<dpp::guild_member>(p_guildUser2); }
        inline void set_callback1(bool p_callback) { _callback1 = p_callback; }
        inline void set_callback2(bool p_callback) { _callback2 = p_callback; }
        inline bool callbackDone() { _callback1 & _callback2; }
};

