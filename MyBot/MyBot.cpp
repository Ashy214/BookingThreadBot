#include "MyBot.h"
#include <dpp/dpp.h>
#include <iostream>
#include <fstream>
#include "BookingInfo.h"
#include <coroutine>

const dpp::snowflake GUILD_ID = 635182631754924032;
std::shared_ptr<std::map<int, BookingInfo>> g_bookedTables = std::make_shared<std::map<int, BookingInfo>>();
std::shared_ptr<std::map<int, dpp::snowflake>> g_tableMessages = std::make_shared<std::map<int, dpp::snowflake>>();
dpp::cache<dpp::message> g_message_cache;
std::mutex g_cache_mtx;
dpp::snowflake g_channel_id = 1255535312528998420; //Hard coded for now. This should be updated to the booking channel created each week
static std::mutex g_booking_mtx;

//ToDo::
// List tables just for that specific game system
// Allow booking for more than 2 people
// Image of table booking to allow user to select from image
// BookTable currently only handles slash commands. Would be useful to allow for someone to book with freeform input, perhaps from a ? command
// Create a bookingThreadTester that writes messages into discord to do the book/remove functions etc
//Report function - The file stored on disk with table bookings will contain snowflake ID's. Not human readable! So will need a function that can go through a bookingFile and convert to usernames.
// Create a pinned message at top of channel showing current bookings, maybe in an image. Would need to get the message ID to edit and potentially store it in file in case bot crashes?
//Need to read through messages on startup to populate g_tableMessages so we can delete if we remove a booking. Should only really need to be done for the event that we crash and restart after a thread has gone live
//Periodically scan messages in channel to check when one has been deleted. Perhaps either on activation (When a slash command comes in)? Might be slow so maybe only on an update
//Need to double check we got nullptr when doing normal message, then removing, then booking then removing again
//Need to change the isOwner check to also store the booker and allow deletion if they were the one who booked it.

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

dpp::task<void> populateGuildMembers(BookingInfo p_bookInfo, dpp::cluster &p_bot, int p_tableNum)
{
	dpp::confirmation_callback_t confirmation;
	dpp::guild_member_map guildMap;

	if (!p_bookInfo.getUser1String().empty())
	{
		confirmation = co_await p_bot.co_guild_search_members(GUILD_ID, p_bookInfo.getUser1String(), 1);

		if (confirmation.is_error()) { /* catching an error to log it */
			p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
			co_return;
		}
		guildMap = confirmation.get<dpp::guild_member_map>();

		//Use bookInfo object from global var as the one passed into coroutine may have changed state by the time we return
		if (!guildMap.empty())
		{
			g_bookedTables->at(p_tableNum).set_user1Member(guildMap.begin()->second);
		}
		//printf("callback1 done");
	}
	if (!p_bookInfo.getUser2String().empty())
	{
		confirmation = co_await p_bot.co_guild_search_members(GUILD_ID, p_bookInfo.getUser2String(), 1);

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
		for (auto g = g_message_cache.get_container().begin(); g != g_message_cache.get_container().end(); ++g) {
			dpp::message* msg = g->second;
			g_message_cache.remove(msg);
		}
		g_cache_mtx.unlock();
	}
}

//This should only really be called on startup. We then maintain g_bookedTables as an up-to-date list of bookings
dpp::task<void> setupBookedTables(dpp::cluster &p_bot)
{
	BookingInfo emptyBooking;
	dpp::confirmation_callback_t confirmation;
	g_booking_mtx.lock();
	//Populate g_bookedTables g_tableMessages
	for (int i = 1; i <= 13; i++)
	{
		g_bookedTables->insert({ i, emptyBooking });
		g_tableMessages->insert({ i, 0 });
	}

	std::string line;
	std::ifstream bookingFile("testfile.txt");
	if (bookingFile.is_open())
	{
		//This reads each line from file, then splits into tokens delimited by ','
		while (getline(bookingFile, line))
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
			BookingInfo currentBooking(bookingLine[0], bookingLine[1], bookingLine[3], p_bot);
			int tableNum = std::stoi(bookingLine[2]);
			g_bookedTables->at(tableNum) = currentBooking;
			populateGuildMembers(currentBooking, p_bot, tableNum);
		}
		bookingFile.close();
	}
	g_booking_mtx.unlock();
	//Now read through message history to get all bookings the bot has made, so the /remove command can work
	confirmation = co_await p_bot.co_messages_get(g_channel_id, 0, 0, 0, 100);
	if (confirmation.is_error()) { /* catching an error to log it */
		p_bot.log(dpp::loglevel::ll_error, confirmation.get_error().message);
		co_return;
	}

	//First clear cache as we will repopulate now
	clearMsgCache();

	dpp::message_map mmap = confirmation.get<dpp::message_map>();
	//Iterate over mmap looking for messages from ourselves
	for (auto& [key, msg] : mmap)
	{
		if (msg.author != p_bot.me.id)
		{
			continue;
		}
		int tableNum = extractFromMsg(&msg);
		if (tableNum >= 1 && tableNum <= 13)
		{
			g_cache_mtx.lock();
			//Successful so update g_tableMessages and store in cache
			g_tableMessages->at(tableNum) = msg.id;
			//Create new pointer and copy value of msg into it, as we relinquish this to cache when we store
			dpp::message* m = new dpp::message;
			*m = msg;
			g_message_cache.store(m);
			g_cache_mtx.unlock();
		}
	}
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
	std::ofstream bookingFile("testfile.txt");
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

//Writes new booking into file
//Will need mutex here to ensure multiple clashes don't happen?
//int writeBookedTable(int p_tableNum)
//{
//	g_file_mtx.lock();
//	std::ofstream bookingFile("testfile.txt", std::ios::app);
//	if (bookingFile.is_open())
//	{
//		bookingFile << formatBookInfo(g_bookedTables->at(p_tableNum), p_tableNum);
//		bookingFile.close();
//	}
//	else
//	{
//		g_file_mtx.unlock();
//		return -6;
//	}
//	g_file_mtx.unlock();
//	return 0;
//}

int setupCommands(dpp::cluster &p_bot)
{
	//setupCommands function called runonce and on /update command. In there iterate over the command map and setup the command options
	std::map<std::string, std::string> listCommands = { {"book",	"Make a table booking"},
														{"remove",	"Delete a table booking"} ,
														{"modify",	"Change a table booking"} ,
														{"help",	"Info on booking usage"},
														{"update",	"Update commands"} };
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
			newCommand.add_option(dpp::command_option(dpp::co_string, "user1", "User 1 to book for", false));
			newCommand.add_option(dpp::command_option(dpp::co_string, "user2", "User 2 to book for", false));
			newCommand.add_option( dpp::command_option(dpp::co_integer,"table",	"Table number", false) );
			newCommand.add_option( dpp::command_option(dpp::co_string, "system", "Game system e.g. 40k/AoS/Kill Team", false) );
			newCommand.add_option(dpp::command_option(dpp::co_user, "userdiscord1", "Optional User 1 to book for. guest name", false) );
			//newCommand.add_option(dpp::command_option(dpp::co_string, "optionalguest2", "Optional User 2 to book for. guest name", false));
		}
		else if (it.first == "remove")
		{
			//newCommand.add_option(dpp::command_option(dpp::co_string, "user",  "User booking to remove", false));
			newCommand.add_option(dpp::command_option(dpp::co_integer, "table", "Table booking to remove", false));
		}
		else if (it.first == "modify")
		{
			newCommand.add_option(dpp::command_option(dpp::co_string, "user", "User booking to modify", false));
			newCommand.add_option(dpp::command_option(dpp::co_string, "table", "Table booking to modify", false));
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
	BookingInfo bookInfo(user1, user2, system, p_bot, creator);
	g_booking_mtx.lock();
	int rc = bookTable(bookInfo, tableNum);
	if(rc == 0
	|| rc == -5)
	{
		//Booking success so format then output booking message
		co_await populateGuildMembers(bookInfo, p_bot, tableNum);
		
		//Use g_bookedTables from here onwards as bookInfo was just for checking booking/passing through data
		dpp::message msg(event.command.channel_id, g_bookedTables->at(tableNum).formatMsg(tableNum));
		p_bot.message_create(msg);
	}
	g_booking_mtx.unlock();
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
int removeBooking(dpp::cluster& p_bot, const dpp::slashcommand_t& event)
{
	int tableNum = static_cast<int>(std::get<int64_t>(event.get_parameter("table")));
	dpp::user creator = event.command.get_issuing_user();

	if (tableNum < 1 || tableNum > 13)
	{
		return -2;
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
				p_bot.co_message_delete(msg->id, msg->channel_id);
				g_message_cache.remove(msg);
				g_cache_mtx.unlock();
				return 0;
			}
			g_cache_mtx.unlock();
			return -9;
		}
		else
		{
			g_booking_mtx.unlock();
			return -8;
		}
	}
	else
	{
		g_booking_mtx.unlock();
		return -7;
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
			return "You do not have permission to remove this booking";
		case -9:
			return "Booking removed, but cannot delete booking message as it was not made via the bot. Please remove it manually";
		default:
			return "Error running command. Please contact @Windsurfer. Error code: " + std::to_string(p_rc);
	}
}

int main()
{
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
			setupBookedTables(bot);
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
		//Could put this all in a try/catch where exception message is output
		if (cmdValue == "update") {
			updateMsg = ((rc = setupCommands(bot)) != 0) ? formatError(rc) : "Successfully updated";
		}
		else if (cmdValue == "book") {
			//For booking, we could also use a modal (form), a select menu (text menu with drop downs) or a clickable image as alternative options
			updateMsg = ((rc = co_await bookTable(bot, event)) != 0) ? formatError(rc) : "Successfully booked";
			//event.edit_original_response(dpp::message(updateMsg).set_flags(dpp::m_ephemeral));
		}
		else if (cmdValue == "modify")
		{
			//This probably needs to send back a modal (form) with the info pre-filled and let the user modify it to 'edit' the booking
			updateMsg = "Not implemented yet";
		}
		else if (cmdValue == "remove")
		{
			updateMsg = ((rc = removeBooking(bot, event)) != 0) ? formatError(rc) : "Successfully removed";
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
