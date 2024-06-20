#include "MyBot.h"
#include <dpp/dpp.h>
#include <iostream>
#include <fstream>
#include "BookingInfo.h"
#include <coroutine>
#include <regex>

const dpp::snowflake GUILD_ID = 635182631754924032;
std::shared_ptr<std::map<int, BookingInfo>> g_bookedTables = std::make_shared<std::map<int, BookingInfo>>();

//ToDo::
// List tables just for that specific game system
// Allow booking for more than 2 people
// Image of table booking to allow user to select from image
// BookTable currently only handles slash commands. Would be useful to allow for someone to book with freeform input, perhaps from a ? command
// Create a bookingThreadTester that writes messages into discord to do the book/remove functions etc
//Report function - The file stored on disk with table bookings will contain snowflake ID's. Not human readable! So will need a function that can go through a bookingFile and convert to usernames.
// Create a pinned message at top of channel showing current bookings, maybe in an image. Would need to get the message ID to edit and potentially store it in file in case bot crashes?
// Make it compatible with others posting bookings so we keep up-to-date reference of current bookings
// Update g_bookedTables to hold pointers to BookingInfo objects instead. That way we can replace empty bookings with the new object addresses

//We can use p_bot.guild_get_member to get the member object from snowflake. We can then get mention@ and nickname for them. For string we could use guild_search_members?
/*
*	We take strings as users. When we create BookingInfo object, we guild_search_members to find snowflake if possible and store in obj. When we output successful booking,
*	if !snowflake.empty() then trigger mention. If it is then output string
*	When we write to file, always use string.
*/
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

//This should only really be called on startup. We then maintain g_bookedTables as an up-to-date list of bookings
void setupBookedTables(dpp::cluster &p_bot)
{
	BookingInfo emptyBooking;
	//Populate g_bookedTables
	for (int i = 1; i <= 13; i++)
	{
		g_bookedTables->insert({ i, emptyBooking });
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
			//We should now have bookingLine containing user1, user2, table, system
			BookingInfo currentBooking(bookingLine[0], bookingLine[1], bookingLine[3], p_bot);
			int tableNum = std::stoi(bookingLine[2]);
			g_bookedTables->at(tableNum) = currentBooking;
			populateGuildMembers(currentBooking, p_bot, tableNum);
		}
		bookingFile.close();
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

//Writes new booking into file and updates g_bookedTables
//Will need mutex here to ensure multiple clashes don't happen?
int writeBookedTable(std::map<int, BookingInfo>::iterator &p_it)
{
	std::ofstream bookingFile("testfile.txt", std::ios::app);
	if (bookingFile.is_open())
	{
		bookingFile << formatBookInfo(p_it->second, p_it->first);
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
	auto it = g_bookedTables->find(p_tableNum);
	//We should never see it == g_bookedTables.end() because we validate tableNum is in range of available tables
	if (!it->second.isBooked())
	{
		//Mutex lock
		it->second = p_bookInfo; //To fix the need to use g_bookedTables going forward we could create a copy operator to change addresses to be equal
		writeBookedTable(it);
		//Mutex unlock
		if (!it->second.isSuitable(p_tableNum))
		{
			return -5;
		}
		return 0;
	}
	//int rc = it->second.isBooked() ? -1 : writeBookedTable(it);
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
			//Release Mutex
			
			//We now need to remove the booking message in the thread

			return 0;
		}
		else
		{
			return -8;
		}
	}
	else
	{
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
			return "Table already booked";
		case -5:
			return "Successfully booked, but table is not recommended for game system selected";
		case -6:
			return "Error accessing booking file";
		case -7:
			return "No booking to remove";
		case -8:
			return "You do not have permission to remove this booking";
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
	dpp::cache<dpp::message> message_cache;

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

	bot.on_message_create([&message_cache, &bot](const dpp::message_create_t& event) {
		/* Make a permanent pointer using new, for each message to be cached */
		dpp::message* m = new dpp::message();
		/* Store the message into the pointer by copying it */
		*m = event.msg;
		//Bail out if it is us posting message
		if (bot.me == m->author)
		{
			return;
		}
		//Parse message to check if it is a 'booking' i.e. has a table number
		//Too difficult to parse whether it contains user vs user etc, so easier to take table number then set creator as the 'user' who booked it
		std::string content = m->content;
		std::transform(content.begin(), content.end(), content.begin(), ::tolower); //Convert to lower case
		if (size_t found = content.find("table ") != std::string::npos)
		{
			//Found Table keyword so need to get the number
			int tableNum = std::stoi(content.substr(found + 5, 2));
			if (tableNum >= 1 || tableNum <= 13)
			{
				//We have valid table number so try to 'book' the slot as long as it's not already booked
				if (!g_bookedTables->at(tableNum).isBooked())
				{
					g_bookedTables->at(tableNum).set_user1(m->author.username);
					dumpTableBookings();
				}
			}
		}

		

		//If it is a booking, store in our g_bookedTables and write to file
		

		/* Store the new pointer to the cache using the store() method */
		message_cache.store(m);
		message_cache.remove(m);
		});

	//Add a handler to accept normal commands, maybe starting with ! e.g. !book <user1> <user2> <tableNum> <system>

	/* Start the bot */
	bot.start(dpp::st_wait);

	return 0;
}
