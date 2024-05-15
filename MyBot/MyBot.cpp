#include "MyBot.h"
#include <dpp/dpp.h>
#include <iostream>
#include <fstream>
#include "BookingInfo.h"

/* Be sure to place your token in the line below.
 * Follow steps here to get a token:
 * https://dpp.dev/creating-a-bot-application.html
 * When you invite the bot, be sure to invite it with the 
 * scopes 'bot' and 'applications.commands', e.g.
 * https://discord.com/oauth2/authorize?client_id=940762342495518720&scope=bot+applications.commands&permissions=139586816064
 */
//const std::string    BOT_TOKEN;
std::shared_ptr<std::map<int, bool>> g_bookedTables = std::make_shared<std::map<int, bool>>();

//ToDo::
// List tables just for that specific game system
// Warning if table selected isn't suitable for that game system
// Allow booking for more than 2 people
// Image of table booking to allow user to select from image
// BookTable currently only handles slash commands. Would be useful to allow for someone to book with freeform input, perhaps from a ? command
// Create a bookingThreadTester that writes messages into discord to do the book/remove functions etc

//This should only really be called on startup. We then maintain g_bookedTables as an up-to-date list of bookings
void setupBookedTables()
{
	//Populate g_bookedTables
	for (int i = 1; i <= 13; i++)
	{
		g_bookedTables->insert({ i, false });
	}

	//Create booking class that accepts a string as constructor, or 4 parms. Then can read text file straight into booking class
	//When reading file, probably want to read entire line and then break it delimited by ','
	std::string line;
	std::ifstream bookingFile("testfile.txt");
	if (bookingFile.is_open())
	{
		while (getline(bookingFile, line))
		{
			std::cout << line << '\n';
		}
		bookingFile.close();
	}
}

//Writes current state of table bookings to file
int writeBookedTables()
{
	std::ofstream bookingFile("testfile.txt", std::ios::app);
	if (bookingFile.is_open())
	{
		bookingFile << "ash,testUser,table 3,40k";
		bookingFile.close();
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

	for (auto const &it : listCommands)
	{
		//Build up vector of commands based on listCommands
		dpp::slashcommand newCommand(it.first, it.second, botId);

		//setup command options for each
		if (it.first == "book")
		{
			newCommand.add_option( dpp::command_option(dpp::co_string, "user1", "User 1 to book for", true) );
			newCommand.add_option( dpp::command_option(dpp::co_string, "user2", "User 2 to book for", true) );
			newCommand.add_option( dpp::command_option(dpp::co_string, "table",	 "Table number", true) );
			newCommand.add_option( dpp::command_option(dpp::co_string, "system", "Game system e.g. 40k/AoS/Kill Team", true) );
		}
		else if (it.first == "remove")
		{
			newCommand.add_option(dpp::command_option(dpp::co_string, "user",  "User booking to remove", false));
			newCommand.add_option(dpp::command_option(dpp::co_string, "table", "Table booking to remove", false));
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
	p_bot.global_bulk_command_create(commands);

	return 0;
}

int bookTable(dpp::cluster &p_bot, const dpp::slashcommand_t &event)
{
	int rc = 0;
	//Should also handle putting an image of the tables and letting user pick from image?
	std::string user1 = std::get<std::string>(event.get_parameter("user1"));
	std::string user2 = std::get<std::string>(event.get_parameter("user2"));
	std::string table = std::get<std::string>(event.get_parameter("table"));
	std::string system = std::get<std::string>(event.get_parameter("system"));
	int tableNum = std::stoi(table);

	//Parameter checking for errors
	if (user1.empty() && user2.empty())
	{
		event.reply(dpp::message("At least one user is required to book a table").set_flags(dpp::m_ephemeral));

	}
	else if (table.empty()
		|| tableNum < 1 //what happens here with stoi if table not convertable to int?? need to catch exception?
		|| tableNum > 13)
	{
		event.reply(dpp::message("Table number between 1-13 required").set_flags(dpp::m_ephemeral));

	}
	else if (system.empty()) //Could also check here that table matches game system suitability
	{
		event.reply(dpp::message("Game system required").set_flags(dpp::m_ephemeral));

	}

	g_bookedTables->find(tableNum)->second ? rc = -1 : rc = writeBookedTables();
	
	return rc;
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
	dpp::cluster bot(BOT_TOKEN);

	/* Output simple log messages to stdout */
	bot.on_log(dpp::utility::cout_logger());

	/* Register slash command here in on_ready */
	bot.on_ready([&bot](const dpp::ready_t& event) {
		/* Wrap command registration in run_once to make sure it doesnt run on every full reconnection */
		if (dpp::run_once<struct register_bot_commands>()) {
			if (setupCommands(bot) != 0)
			{
				printf("Error setting up initial commands");
			}
			setupBookedTables();
		}
	});

	/* Handle slash command */
	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {
		auto cmdValue = event.command.get_command_name();
		std::string updateMsg;

		//Could put this all in a try/catch where exception message is output
		if (cmdValue == "ping") {
			event.reply("Pong!");
		}
		else if (cmdValue == "update") {
			updateMsg = setupCommands(bot) != 0 ? "Error updating commands" : "Successfully updated";
		}
		else if (cmdValue == "book") {
			updateMsg = bookTable(bot, event) != 0 ? "Error booking table" : "Successfully booked";
		}
		else if (cmdValue == "modify")
		{
			updateMsg = "Nothing yet";
		}
		else if (cmdValue == "remove")
		{
			updateMsg = "Nothing yet";
		}
		event.reply(dpp::message(updateMsg).set_flags(dpp::m_ephemeral));
	});

	/* Start the bot */
	bot.start(dpp::st_wait);

	return 0;
}
