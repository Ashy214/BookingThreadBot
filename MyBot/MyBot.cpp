#include "MyBot.h"
#include <dpp/dpp.h>
#include <iostream>
#include <fstream>
#include "BookingInfo.h"
#include <coroutine>
#include <ctime>
#include <chrono>
#include "date.h"

dpp::snowflake GUILD_ID;
dpp::snowflake CATEGORY_ID;
dpp::snowflake g_admin_chan_id;
dpp::snowflake g_archive_cat;
std::shared_ptr<std::map<int, BookingInfo>> g_bookedTables = std::make_shared<std::map<int, BookingInfo>>();
std::shared_ptr<std::map<int, dpp::snowflake>> g_tableMessages = std::make_shared<std::map<int, dpp::snowflake>>();
dpp::cache<dpp::message> g_message_cache;
std::mutex g_cache_mtx;
dpp::snowflake g_channel_id;
static std::mutex g_booking_mtx;
bool g_auto = false;
std::string g_bookingFile;
std::string g_botInfo = "botInfo.txt";

//Future features:
// List tables just for that specific game system
// Allow booking for more than 2 people
// Image of table booking to allow user to select from image
// BookTable currently only handles slash commands. Would be useful to allow for someone to book with freeform input, perhaps from a ? command
// Create a pinned message at top of channel showing current bookings, maybe in an image. Would need to get the message ID to edit and potentially store it in file in case bot crashes?
// Periodically scan messages in channel to check when one has been deleted. Perhaps either on activation (When a slash command comes in)? Might be slow so maybe only on an update. Alternatively could cache ALL messages and if we find it's booked check message ID and see if msg still exists?
// Might need some way to corroborate between bookingFile and messages in channel. Maybe a separate command to keep them in sync?
// Work on automatic posting of booking thread each week, or allow a date to be set? Perhaps kick off a new thread that rusn the newchannel function but with an optional delay as a parm to fire it off when needed?
// Looking for game system. /LFG to sign up for a system, or to see whos LFG for a specific system. Can 'apply' to play a game and have user accept it etc.
// Some way of checking in people on the night. Check for an reaction on the booking msg and can fill out whatever committee uses to see who's paid the night etc. 

//ToDo:
//	Update newChannel function to actually wait until the date specified to post (still post 6 days before it though). Only do this for auto I think
//	Some method of tracking if a booking thread has been created not using the bot and updating the IDs to find it. Maybe running /update to scan. Easiest will be to use an on_channel event to monitor for it

//Done:
// Bug in populateGuildMembers on readBookingFile wasn't updating the global booking objects, so when we tried to remove booking after a restart we had no idea if user was the one who made booking or not. Shifted populateGuildMembers to new function run after we setup tables. I suspect it's an issue with coroutines
// When creating a new channel, we now move it to the archive category instead of deleting. This is a short-term solution as categories have limits on number of channels. 


void handle_eptr(std::exception_ptr eptr) // passing by value is ok
{
	try
	{
		if (eptr)
			std::rethrow_exception(eptr);
	}
	catch (const std::exception& e)
	{
		std::cout << "Caught exception: '" << e.what() << "'\n";
	}
}

dpp::task<void> populateGuildMembers(BookingInfo *p_bookInfo, dpp::cluster &p_bot, int p_tableNum)
{
	dpp::confirmation_callback_t confirmation;
	dpp::guild_member_map guildMap;

	if (!p_bookInfo->getUser1String().empty())
	{
		confirmation = co_await p_bot.co_guild_search_members(GUILD_ID, p_bookInfo->getUser1String(), 1);

		if (confirmation.is_error()) { /* catching an error to log it */
			p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
			//Don't co_return here as we still need to check for user2
		}
		else
		{
			guildMap = confirmation.get<dpp::guild_member_map>();

			//Use bookInfo object from global var as the one passed into coroutine may have changed state by the time we return
			if (!guildMap.empty())
			{
				g_bookedTables->at(p_tableNum).set_user1Member(guildMap.begin()->second);
			}
		}
	}
	if (!p_bookInfo->getUser2String().empty())
	{
		confirmation = co_await p_bot.co_guild_search_members(GUILD_ID, p_bookInfo->getUser2String(), 1);
		if (confirmation.is_error()) { /* catching an error to log it */
			p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
			co_return;
		}
		guildMap = confirmation.get<dpp::guild_member_map>();
		//Use bookInfo object from global var as the one passed into coroutine may have changed state by the time we return
		if (!guildMap.empty())
		{
			g_bookedTables->at(p_tableNum).set_user2Member(guildMap.begin()->second);
		}
		//printf("callback2 done");
	}
	
	co_return;
}

int extractFromMsg(dpp::message *p_msg)
{
	//Parse message to check if it is a 'booking' i.e. has a table number
	std::string content = p_msg->content;
	std::transform(content.begin(), content.end(), content.begin(), ::tolower); //Convert to lower case
	//Found table keyword so need to get the number
	std::stringstream ss(content);
	std::string token;
	std::vector<std::string> bookingLine;
	while (getline(ss, token, '\n'))
	{
		if (size_t strStart = token.find("table ") != std::string::npos)
		{
			//Found the line with table in it
			return std::stoi(token.substr(strStart + 5, 2));
		}
	}
	return 0;
}

//Loop through cache and remove all messages
void clearMsgCache()
{
	if (g_message_cache.count() > 0)
	{
		g_cache_mtx.lock();
		auto g = g_message_cache.get_container();
		for (auto i = g.begin(); i != g.end(); i++) {
			dpp::message* msg = i->second;
			g_message_cache.remove(msg);
		}
		g_cache_mtx.unlock();
	}
}

//Dumps info bot needs on restart, such as current booking file name and current channel ID of booking thread
int dumpBotInfo()
{
	std::ofstream botFile(g_botInfo);
	if (botFile.is_open())
	{
		botFile << g_bookingFile << '\n';
		botFile << g_channel_id;
		botFile.close();
	}
	else
	{
		return -11;
	}
	return 0;
}

//populates info bot needs on restart, such as current booking file name and current channel ID of booking thread
std::vector<std::string> getBotInfo(std::string p_file)
{
	std::string line;
	std::ifstream botInfo(p_file);
	std::vector<std::string> botInfoLines;
	if (botInfo.is_open())
	{
		//This reads each line from file which should be:
		//	g_bookingFile
		//	g_channel_id
		while (getline(botInfo, line))
		{
			std::cout << line << '\n';
			botInfoLines.push_back(line);
		}
		botInfo.close();
	}
	return botInfoLines;
}

dpp::task<void> setupMessageHistory(dpp::cluster& p_bot)
{
	//Now read through message history to get all bookings the bot has made, so the /remove command can work
	//There seems to be a bug that this throws an Error: 50001: Missing Access if the channel we're reading from is empty...
	dpp::confirmation_callback_t confirmation = co_await p_bot.co_messages_get(g_channel_id, 0, 0, 0, 100);
	if (confirmation.is_error()) { /* catching an error to log it */
		p_bot.log(dpp::loglevel::ll_warning, confirmation.get_error().message);
		co_return;
	}

	//First clear cache as we will repopulate now
	clearMsgCache();

	dpp::message_map mmap = confirmation.get<dpp::message_map>();
	//Iterate over mmap looking for messages from ourselves
	g_cache_mtx.lock();
	for (auto& [key, msg] : mmap)
	{
		if (msg.author != p_bot.me.id)
		{
			continue;
		}
		int tableNum = extractFromMsg(&msg);
		if (tableNum >= 1 && tableNum <= 13)
		{
			//Successful so update g_tableMessages and store in cache
			g_tableMessages->at(tableNum) = msg.id;
			//Create new pointer and copy value of msg into it, as we relinquish this to cache when we store
			dpp::message* m = new dpp::message;
			*m = msg;
			g_message_cache.store(m);
		}
	}
	g_cache_mtx.unlock();
	co_return;
}

std::string readBookingFile(dpp::cluster &p_bot, std::ifstream &p_bookFile, bool p_list)
{
	std::string listOutput;
	std::string line;
	if (p_bookFile.is_open())
	{
		//This reads each line from file, then splits into tokens delimited by ','
		while (getline(p_bookFile, line))
		{
			std::cout << line << '\n';
			std::stringstream ss(line);
			std::string token;
			std::vector<std::string> bookingLine;
			while (getline(ss, token, ','))
			{
				bookingLine.push_back(token);
			}
			//We might not have a system declared, so manually add one
			if (bookingLine.size() == 3)
			{
				bookingLine.push_back("Other");
			}
			//We should now have bookingLine containing user1, user2, table, system
			BookingInfo currentBooking(bookingLine[0], bookingLine[1], bookingLine[3]);
			int tableNum = std::stoi(bookingLine[2]);
			if (p_list)
			{
				//Build up msg to output to user
				listOutput.append(line);
				listOutput.append("\n");
			}
			else
			{
				g_bookedTables->at(tableNum) = currentBooking;
				//populateGuildMembers(&g_bookedTables->at(tableNum), p_bot, tableNum);
			}
		}
		p_bookFile.close();
	}
	return listOutput;
}

//This should only really be called on startup. We then maintain g_bookedTables as an up-to-date list of bookings
dpp::task<void> setupBookedTables(dpp::cluster &p_bot)
{
	std::vector<std::string> botInfo = getBotInfo(g_botInfo);
	if (botInfo.size() == 0)
	{
		p_bot.log(dpp::loglevel::ll_error, "Error reading botInfo file");
	}
	g_bookingFile = botInfo[0];
	g_channel_id = botInfo[1];
	
	//This are constant IDs that never change, but having them stored in a file makes it easier to have the bot running on multiple machines for testing purposes
	botInfo = getBotInfo("chanids.txt");
	if (botInfo.size() == 0)
	{
		p_bot.log(dpp::loglevel::ll_error, "Error reading chanids file");
	}
	GUILD_ID = botInfo[0];
	CATEGORY_ID = botInfo[1];
	g_admin_chan_id = botInfo[2];
	g_archive_cat = botInfo[3];
	g_bookedTables->clear();
	BookingInfo emptyBooking;
	g_booking_mtx.lock(); //May need to remove this if we ever have a case where this function could be run in quick succession (The coawait in populateGuildMembers could mean another command comes in an runs this, which will crash on the 2nd acquisition of this mutex)
	//Populate g_bookedTables g_tableMessages
	for (int i = 1; i <= 13; i++)
	{
		g_bookedTables->insert({ i, emptyBooking });
		g_tableMessages->insert({ i, 0 });
	}

	std::string line;
	std::ifstream bookingFile(g_bookingFile);
	readBookingFile(p_bot, bookingFile, false);
	g_booking_mtx.unlock();
	co_return;
}

std::string formatBookInfo(BookingInfo &p_bookInfo, int p_tableNum)
{
	std::string user1 = p_bookInfo.getUser1String();
	std::string user2 = p_bookInfo.getUser2String();
	std::string system = p_bookInfo.getSystem();
	return user1 + ',' + user2 + ',' + std::to_string(p_tableNum) + ',' + system + '\n';
}

//Dumps all table bookings to current file
int dumpTableBookings()
{
	std::ofstream bookingFile(g_bookingFile);
	if (bookingFile.is_open())
	{
		//Iterate through map dumping table num and bookingInfo to file
		for (auto it = g_bookedTables->begin(); it != g_bookedTables->end(); it++)
		{
			bookingFile << formatBookInfo(it->second, it->first);
		}
		bookingFile.close();
	}
	else
	{
		return -6;
	}
	return 0;
}

int setupCommands(dpp::cluster &p_bot)
{
	//setupCommands function called runonce and on /update command. In there iterate over the command map and setup the command options
	std::map<std::string, std::string> listCommands = { {"book",	"Make a table booking"},
														{"remove",	"Delete a table booking"} ,
														{"modify",	"Change a table booking"} ,
														//{"help",	"Info on booking usage"} ,
														{"update",	"Update commands"} ,
														{"channel", "Create a new booking channel"} ,
														{"auto",    "Set auto booking thread creation"} ,
														{"list",    "List current bookings"} };
	std::vector<dpp::slashcommand> commands;
	auto botId = p_bot.me.id;
	p_bot.guild_bulk_command_delete(GUILD_ID);
	for (auto const &it : listCommands)
	{
		//Build up vector of commands based on listCommands
		dpp::slashcommand newCommand(it.first, it.second, botId);

		//setup command options for each
		if (it.first == "book")
		{
			//newCommand.add_option( dpp::command_option(dpp::co_user, "user1", "User 1 to book for", false) );
			//newCommand.add_option( dpp::command_option(dpp::co_user, "user2", "User 2 to book for", false) );
			newCommand.add_option(dpp::command_option(dpp::co_string, "user1", "User 1 to book for", true));
			newCommand.add_option(dpp::command_option(dpp::co_string, "user2", "User 2 to book for", true));
			newCommand.add_option( dpp::command_option(dpp::co_integer,"table",	"Table number", true) );
			newCommand.add_option( dpp::command_option(dpp::co_string, "system", "Game system e.g. 40k/AoS/Kill Team", true) );
			newCommand.add_option(dpp::command_option(dpp::co_user, "userdiscord1", "Optional User 1 to book for. guest name", false) );
			//newCommand.add_option(dpp::command_option(dpp::co_string, "optionalguest2", "Optional User 2 to book for. guest name", false));
		}
		else if (it.first == "remove")
		{
			//newCommand.add_option(dpp::command_option(dpp::co_string, "user",  "User booking to remove", false));
			newCommand.add_option(dpp::command_option(dpp::co_integer, "table", "Table booking to remove", true));
		}
		else if (it.first == "modify")
		{
			newCommand.add_option(dpp::command_option(dpp::co_string, "user", "User booking to modify", false));
			newCommand.add_option(dpp::command_option(dpp::co_string, "table", "Table booking to modify", false));
		}
		else if (it.first == "channel")
		{
			newCommand.add_option(dpp::command_option(dpp::co_string, "date", "date in format DD-MM-YYYY", false));
		}
		//No options for help or update
		commands.push_back(newCommand);
	}
	if(commands.size() == 0)
	{
		//Log error
		return -1;
	}
	p_bot.guild_bulk_command_create(commands, GUILD_ID);

	return 0;
}

//This assumes p_bookInfo has been parsed and is valid (no empty user, game system etc)
int bookTable(BookingInfo p_bookInfo, int p_tableNum)
{
	BookingInfo* actualBooking = &g_bookedTables->at(p_tableNum);
	if (!actualBooking->isBooked())
	{
		*actualBooking = p_bookInfo;
		dumpTableBookings();
		//writeBookedTable(p_tableNum);
		if (!actualBooking->isSuitable(p_tableNum))
		{
			return -5;
		}
		return 0;
	}
	return -4;
}

dpp::task<int> bookTable(dpp::cluster &p_bot, const dpp::slashcommand_t &event)
{
	//Should also handle putting an image of the tables and letting user pick from image?
	//dpp::snowflake user1 = std::get<dpp::snowflake>(event.get_parameter("user1"));
	//dpp::snowflake user2 = std::get<dpp::snowflake>(event.get_parameter("user2"));
	std::string user1 = std::get<std::string>(event.get_parameter("user1"));
	std::string user2 = std::get<std::string>(event.get_parameter("user2"));
	std::string system = std::get<std::string>(event.get_parameter("system"));
	dpp::user creator = event.command.get_issuing_user();
	int tableNum = static_cast<int>(std::get<int64_t>(event.get_parameter("table")));
	p_bot.log(dpp::loglevel::ll_info, "Options: " + user1 + "," + user2 + "," + system + "," + std::to_string(tableNum));
	//dpp::snowflake userId = std::get<dpp::snowflake>(event.get_parameter("userdiscord1"));
	//Parameter checking for errors
	//dpp::guild_member resolved_member = event.command.get_resolved_member(userId);
	if (user1.empty() && user2.empty())
	{
		co_return -1;
	}
	else if(tableNum < 1
		 || tableNum > 13)
	{
		co_return -2;
	}
	else if (system.empty())
	{
		co_return -3;
	}
	
	//Now create bookingInfo object from above parms
	BookingInfo bookInfo(user1, user2, system, creator);
	//g_booking_mtx.lock();
	int rc = bookTable(bookInfo, tableNum);
	if(rc == 0
	|| rc == -5)
	{
		//Booking success so format then output booking message
		co_await populateGuildMembers(&bookInfo, p_bot, tableNum);
		
		//Use g_bookedTables from here onwards as bookInfo was just for checking booking/passing through data
		dpp::message msg(g_channel_id, g_bookedTables->at(tableNum).formatMsg(tableNum));
		p_bot.message_create(msg);
	}
	//g_booking_mtx.unlock();
	co_return rc;
}

//Returns true if user is owner of booking or is administrator, otherwise false
bool hasPerms(const dpp::slashcommand_t& event, dpp::user &p_user, BookingInfo *p_bookInfo)
{
	dpp::permission perms = event.command.get_resolved_permission(p_user.id);
	if (p_bookInfo->isOwner(p_user)
	|| perms.has(dpp::p_administrator))
	{
		return true;
	}
	return false;
}

//Remove a booking from g_bookedTables and delete the corresponding message posted in the channel
dpp::task<int> removeBooking(dpp::cluster& p_bot, const dpp::slashcommand_t& event)
{
	int tableNum = static_cast<int>(std::get<int64_t>(event.get_parameter("table")));
	dpp::user creator = event.command.get_issuing_user();

	if (tableNum < 1 || tableNum > 13)
	{
		co_return -2;
	}
	g_booking_mtx.lock();
	BookingInfo *bookInfo = &g_bookedTables->at(tableNum);
	//Check there is a booking to remove, and if so that it is this user that made the booking or the user is an admin
	if(bookInfo->isBooked())
	{
		if(hasPerms(event, creator, bookInfo))
		{
			//Mutex
			//User has permission to remove booking so go ahead
			bookInfo->clearBooking();
			dumpTableBookings(); //Dumping after deleting the booking in g_bookedTables should effectively 'rewrite' the booking file minus the one we want to remove
			g_booking_mtx.unlock();
			
			//We now need to remove the booking message in the thread
			g_cache_mtx.lock();
			dpp::message* msg = g_message_cache.find(g_tableMessages->at(tableNum));
			if (msg != nullptr)
			{
				//dpp::confirmation_callback_t confirmation = co_await p_bot.co_message_delete(msg->id, msg->channel_id);
				msg->content = "Booking removed by: " + creator.global_name;
				dpp::confirmation_callback_t confirmation = co_await p_bot.co_message_edit(*msg);
				if (confirmation.is_error()) { /* catching an error to log it */
					p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
					//If it's an unknown message, it means someone manually deleted before /remove was run. Continue as normal to remove from cache
					if (confirmation.get_error().message != "Unknown Message")
					{
						g_cache_mtx.unlock();
						co_return -12;
					}
				}
				g_message_cache.remove(msg);
				g_cache_mtx.unlock();
				co_return 0;
			}
			g_cache_mtx.unlock();
			co_return -9;
		}
		else
		{
			g_booking_mtx.unlock();
			co_return -8;
		}
	}
	else
	{
		g_booking_mtx.unlock();
		co_return -7;
	}
}

std::string formatError(int p_rc)
{
	switch (p_rc)
	{
		case -1:
			return "At least one user is required to book a table";
		case -2:
			return "Table number between 1-13 required";
		case -3:
			return "No game system specified";
		case -4:
			return "Table already booked. If there is no booking message please run /update or contact @Windsurfer";
		case -5:
			return "Successfully booked, but table is not recommended for game system selected";
		case -6:
			return "Error accessing booking file";
		case -7:
			return "No booking to remove";
		case -8:
			return "You do not have permission";
		case -9:
			return "Booking removed, but cannot delete booking message as it was not made via the bot. Please remove it manually";
		case -10:
			return "Error creating/deleting channel";
		case -11:
			return "Error accessing botInfo file";
		case -12:
			return "Error editing msg in channel";
		case -13:
			return "Error in checking if channel already exists";
		case -14:
			return "Channel already exists for this week";
		default:
			return "Error running command. Please contact @Windsurfer. Error code: " + std::to_string(p_rc);
	}
}

//Create a new booking channel for the next available Tuesday, or allow a specific date to be input to book for that Tuesday
//When a new channel is created, amend g_channel_id so we post in there going forward, and archive/update the bookingFile
dpp::task<int> newChannel(dpp::cluster& p_bot, const dpp::slashcommand_t& p_event)
{
	//First off we need to check if user is an admin, otherwise immediately return
	dpp::user creator = p_event.command.get_issuing_user();
	dpp::permission perms = p_event.command.get_resolved_permission(creator.id);
	if (!perms.has(dpp::p_administrator))
	{
		co_return -8;
	}

	dpp::channel bookingChannel;
	std::string strDate;
	//There is NO date-picker or 'date' data type that Discord can enforce from the slash command, so we are relying on the user providing the correct input in date format
	//For that reason, we will ONLY accept a date of DD-MM-YYYY to make it easier to parse. If it's wrong we simply reject and don't execute this command
	dpp::command_interaction cmd_data = p_event.command.get_command_interaction();
	if (!cmd_data.options.empty())
	{
		strDate = std::get<std::string>(p_event.get_parameter("date"));
		//For now we don't need this, but in future we will need to calculate days before this date for when to post the thread automatically
		//std::istringstream ss(strDate);
		//std::chrono::system_clock::time_point tp;
		//ss >> date::parse("%d-%m-%Y", tp);
	}
	else
	{
		//Always want to get the next tuesday
		auto todays_date = date::floor<date::days>(std::chrono::system_clock::now());
		auto nextTuesday = todays_date + date::days{ 7 } -
			(date::weekday{ todays_date } - date::Tuesday);
		//std::cout << nextTuesday << '\n';
		strDate = date::format("%d-%m-%Y", nextTuesday);
	}
	std::string newChannelName = "table-booking-" + strDate;
	bool foundChannel = false;
	dpp::channel currentChannel;
	//Now check we don't already have a booking thread for this week (If strDate is the same as the booking thread name it means we must not have passed next tuesday, as strDate always works out next available one)
	dpp::confirmation_callback_t confirmation = co_await p_bot.co_channel_get(g_channel_id);
	if (!confirmation.is_error()) { //If no error, means channel exists so we need to check if it's the current weekly thread or not
		currentChannel = confirmation.get<dpp::channel>();
		foundChannel = true;
		if (currentChannel.name == newChannelName)
		{
			//We have already created a channel for this coming tuesday so exit out, otherwise continue to create new channel
			co_return -14;
		}
	}
	else
	{
		if(confirmation.get_error().message != "Unknown Channel")
		{
			p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message); //Do we hit this error if the channle doesn't exist?!
			co_return -13;
		}
		
	}
	//First dump current bookingInfo out to file
	dumpTableBookings();

	//Now create new channel and archive old
	bookingChannel.set_name(newChannelName);
	bookingChannel.set_guild_id(GUILD_ID);
	bookingChannel.set_parent_id(CATEGORY_ID);
	confirmation = co_await p_bot.co_channel_create(bookingChannel);
	if (confirmation.is_error()) { /* catching an error to log it */
		p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
		co_return -10;
	}
	bookingChannel = confirmation.get<dpp::channel>(); //Get the newly created channel object

	//Now archive the channel
	if (foundChannel)
	{
		//No need to check if currentChannel is null as we will only have set foundChannel if we have a current one
		currentChannel.set_parent_id(g_archive_cat);
		confirmation = co_await p_bot.co_channel_edit(currentChannel);
		if (confirmation.is_error()) { /* catching an error to log it */
			p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
			co_return -10;
		}
	}
	//Potential for a clash without having a mutex on this but extremely unlikely to ever happen (Only if auto thread and manual creation happen at same time)
	g_bookingFile = "SWATBookings-" + strDate  + "-" + bookingChannel.id.str();
	g_channel_id = bookingChannel.id;

	//Update botInfo in case we crash so we can get these ID's + filename back in future
	dumpBotInfo();

	//Now set ourselves back to a 'clean' state with no bookings
	setupBookedTables(p_bot);
	clearMsgCache();
	co_return 0;
}

//Can't create an overloaded newChannel function as that doesn't seem to work with threads
int newChannelDelay(dpp::cluster& p_bot, const dpp::slashcommand_t& p_event, time_t p_timer)
{
	//Now check here that g_auto is still set. If not, we have been disabled so should just exit thread. What if we have disabled/enabled again, we fire both threads?
	if (g_auto)
	{
		newChannel(p_bot, p_event);
	}
	return 0;
}

int autoThread(dpp::cluster& p_bot, const dpp::slashcommand_t& p_event)
{
	//This could work by doing condition variables: https://en.cppreference.com/w/cpp/thread/condition_variable
	//Kick off thread. Work out how long it needs to sleep before posting, then wait on condition variable that says to kill thread. If woken up, check if kill or not and if not then post thread. Issue with spurious wakeups?
	//Maybe just a sep thread constantly running - checks time every 30m. If auto off then exit thread, otherwise check and sleep. If on the right day then just post
	g_auto = !g_auto;
	if (g_auto)
	{
		//Means it was false -> true so start a newChannel function with a delay to post 6 days before if possible, otherwise immediately
		//Do some calculations for a delay here and pass it to newChannelDelay function
		time_t delay = 5; //Hard-coded delay for now
		p_bot.log(dpp::loglevel::ll_error, "delay is: " + delay);

		//Do some additional checks here to see if there is already a thread running. If there is we shouldn't start another one!
		std::thread t(newChannelDelay, std::ref(p_bot), std::ref(p_event), delay);
		
	}
	return 0;
}

std::string listFile(dpp::cluster& p_bot, const dpp::slashcommand_t& p_event)
{
	std::ifstream bookingFile(g_bookingFile);
	return readBookingFile(p_bot, bookingFile, true);
}

dpp::task<void> setupMembers(dpp::cluster& p_bot)
{
	for (int i = 1; i <= 13; i++)
	{
		co_await populateGuildMembers(&g_bookedTables->at(i), p_bot, i);
	}
}

int main()
{
	std::cout << "hello";
	std::string BOT_TOKEN;
	std::ifstream tokenFile("bottoken.txt");
	if (tokenFile.is_open())
	{
		getline(tokenFile, BOT_TOKEN);
	}
	/* Create bot cluster */
	dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_message_content);

	/* Output simple log messages to stdout */
	bot.on_log(dpp::utility::cout_logger());

	/* Register slash command here in on_ready */
	bot.on_ready([&bot](const dpp::ready_t& event)->dpp::task<void> {
		/* Wrap command registration in run_once to make sure it doesnt run on every full reconnection */
		if (dpp::run_once<struct register_bot_commands>()) {
			if (setupCommands(bot) != 0)
			{
				printf("Error setting up initial commands");
			}
			co_await setupBookedTables(bot);
			co_await setupMembers(bot);
			co_await setupMessageHistory(bot);
			co_return;
		}
	});

	/* Handle slash command */
	
	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event)->dpp::task<void> {
		dpp::async thinking = event.co_thinking(true);
		auto cmdValue = event.command.get_command_name();
		std::string updateMsg;
		int rc = 0;
		co_await thinking;

		//Logging of command here
		dpp::user creator = event.command.get_issuing_user();
		bot.log(dpp::loglevel::ll_info, "user: " + creator.username + ". Command: " + cmdValue);
		
		if (event.command.channel_id != g_channel_id && event.command.channel_id != g_admin_chan_id)
		{
			updateMsg = "Commands cannot be run in this channel";
			std::string logMsg = event.command.channel_id.str() + " g_channel_id = " + g_channel_id.str();
			bot.log(dpp::loglevel::ll_error, logMsg);
		}
		//Could put this all in a try/catch where exception message is output
		else if (cmdValue == "update") {
			updateMsg = ((rc = setupCommands(bot)) != 0) ? formatError(rc) : "Successfully updated";
		}
		else if (cmdValue == "book") {
			//For booking, we could also use a modal (form), a select menu (text menu with drop downs) or a clickable image as alternative options
			updateMsg = ((rc = co_await bookTable(bot, event)) != 0) ? formatError(rc) : "Successfully booked";
		}
		else if (cmdValue == "modify")
		{
			//This probably needs to send back a modal (form) with the info pre-filled and let the user modify it to 'edit' the booking
			updateMsg = "Not implemented yet. Please remove and then re-book the table";
		}
		else if (cmdValue == "remove")
		{
			updateMsg = ((rc = co_await removeBooking(bot, event)) != 0) ? formatError(rc) : "Successfully removed";
		}
		else if (cmdValue == "channel")
		{
			updateMsg = ((rc = co_await newChannel(bot, event)) != 0) ? formatError(rc) : "New booking thread created";
		}
		else if (cmdValue == "auto")
		{
			updateMsg = ((rc = autoThread(bot, event)) != 0) ? formatError(rc) : "Auto creation set to " + g_auto;
		}
		else if (cmdValue == "list")
		{
			updateMsg = listFile(bot, event);
		}
		
		event.edit_original_response(dpp::message(updateMsg).set_flags(dpp::m_ephemeral));
	});

	bot.on_message_create([&bot](const dpp::message_create_t& event) {
		dpp::message msg = event.msg;
		int tableNum = extractFromMsg(&msg);
		if (tableNum >= 1 && tableNum <= 13)
		{
			g_booking_mtx.lock();
			//We have valid table number so store in cache along with ID for use when we need to remove it
			if (bot.me == msg.author)
			{
				dpp::message* m = new dpp::message;
				*m = msg;
				g_tableMessages->at(tableNum) = m->id;
				/* Store the new pointer to the cache using the store() method */
				g_message_cache.store(m);
				g_booking_mtx.unlock();
				return;
			}
			//If another user hasn't booked using the bot, check if table is already booked and if not assign it to this user
			if (!g_bookedTables->at(tableNum).isBooked())
			{
				g_bookedTables->at(tableNum).set_user1(msg.author.username);
				g_bookedTables->at(tableNum).set_system("Other");
				dumpTableBookings();
			}
			g_booking_mtx.unlock();
		}
								
		});

	//Add a handler to accept normal commands, maybe starting with ! e.g. !book <user1> <user2> <tableNum> <system>

	/* Start the bot */
	bot.start(dpp::st_wait);

	return 0;
}
