#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
class BookingInfo
{
    private:
        std::string _user1;
        std::string _user2;
        std::string _system;
        std::shared_ptr<std::map<std::string, std::vector<int>>> tableSystem;

    public:
        /// <summary>
        /// Constructor
        /// </summary>
        BookingInfo();

	    /// <summary>
        /// Constructor
        /// </summary>
        BookingInfo(std::string &p_user1, std::string &p_user2, std::string &p_system);

        /// <summary>
        /// Destructor
        /// </summary>
        ~BookingInfo();

        bool isBooked();
        bool isSuitable(int p_tableNum);

        inline std::string getUser1() { return _user1; }
        inline std::string getUser2() { return _user2; }
        inline std::string getSystem() { return _system; }
};

