#include <iostream>
#include <mutex>
#include <random>
#include <condition_variable>

class UniformRandInt
{
public:
	void Init(int min, int max)
	{
		randEngine.seed(randDevice());
		distro = std::uniform_int_distribution<int>(min, max);
	}

	int operator()()
	{
		return distro(randEngine);
	}

private:
	std::random_device randDevice;
	std::mt19937 randEngine;
	std::uniform_int_distribution<int> distro;
	std::mutex randMutex;
};

enum class GameState
{
	StillPlaying,
	Won,
	Draw
};

enum class PlayerType
{
	None,
	X,
	O
};

enum class LogSyncOperation
{
	Init,
	Release,
	Lock,
	Unlock
};

struct Game
{
	int playerCount;
	int gameNumber;
	PlayerType currentTurn;
	GameState currentGameState;
	int playerX;
	int playerO;
	std::mutex playerCountMutex;
	// Primary mutex that controls the game play.
	std::mutex gameMutex;
	// Primary conditional that controls the game play
	std::condition_variable gameCondition;
	// Unique lock which will be constructed with the gameMutex.
	std::unique_lock<std::mutex>* gameUniqueLock;
	// A 3x3 array of PlayerTypes that represents the game board.
	PlayerType gameBoard[3][3];
};

// Contains all player related data
struct Player
{
	// ID of the player
	int id;
	// Number of games this player has played
	int gamesPlayed;
	// Number of games this player won
	int winCount;
	// Number of games this player lost
	int loseCount;
	// Number of games this player tied
	int drawCount;
	// Type of player this player represents
	PlayerType type;
	// Pointer to the pool of games. See GamePool for more details.
	struct GamePool* gamePool;
	// Pointer to the pool of players. See PlayerPool for more details.
	struct PlayerPool* playerPool;
	// random number generator for this thread
	UniformRandInt myRand;

};

// Holds all of the games
struct GamePool
{
	// An array of game specific data with exactly one entry for each game. See Game for more details.
	Game* perGameData;
	// Total number of games and the number of entries in perGameData
	int totalGameCount;
};

// Stores data for keeping track of the total number of player threads
struct PlayerPool
{
	int totalPlayerCount;
	std::mutex totalPlayersMutex;
	std::mutex startingGunMutex;
	std::mutex playersIncrementMutex;
	std::mutex playersDecrementMutex;
	std::condition_variable playerCondition;
	std::condition_variable playerIncrementCondition;
	std::condition_variable playerDecrementCondition;
	bool gunFlag;
};

// Prompts the user to press enter and waits for user input
void Pause()
{
	printf("Press Enter to continue\n");
	getchar();
}

void LogSync(LogSyncOperation operationToPerform)
{
	// Implement 'LogSync' logic. (To be used later on)
	static std::mutex logMutex;
	std::lock_guard<std::mutex> lock(logMutex);
}

// Prints a formatted string to the standard output in a thread safe manner. 
int Log(const char* format, ...)
{
	int result = 0;

	// Implement 'Log' logic. This function behaves exactly like printf,
	LogSync(LogSyncOperation::Lock);
	printf(format);
	LogSync(LogSyncOperation::Unlock);

	return result;
}

// Prints the current game board to the console
void PrintGameBoard(const Game* currentGame)
{
	// Prints the game board to the screen as a single block of text
	LogSync(LogSyncOperation::Lock);

	for (int row = 0; row < 3; row++)
	{
		for (int col = 0; col < 3; col++)
		{
			if (currentGame->gameBoard[row][col] == PlayerType::None)
			{
				printf("[ ]");
			}
			else
			{
				printf("[%c]", (currentGame->gameBoard[row][col] == PlayerType::X) ? 'X' : 'O');
			}
			std::this_thread::yield();
		}
		printf("\n");
	}
	LogSync(LogSyncOperation::Unlock);
}

// Determines if the player made a winning move on the game board
bool DidWeWin(int row, int col, const Game* game, const Player* player)
{
	bool completeRow = true;
	bool completeCol = true;
	bool completeDiagonalA = true;
	bool completeDiagonalB = true;

	// Check specified row, column, and both diagonals to see if this player made
	//  a winning move.
	for (int i = 0; i < 3; i++)
	{
		if (game->gameBoard[row][i] != player->type)
			completeRow = false;
		if (game->gameBoard[i][col] != player->type)
			completeCol = false;
		if (game->gameBoard[i][i] != player->type)
			completeDiagonalA = false;
		if (game->gameBoard[2 - i][i] != player->type)
			completeDiagonalB = false;
	}

	return completeRow || completeCol || completeDiagonalA || completeDiagonalB;
}

// Play the entire game of Tic-Tac-Toe as 'currentPlayer' in 'currentGame'
GameState MakeAMove(Player* currentPlayer, Game* currentGame)
{
	int possibleMoves[9];
	int totalPossibleMoves = 0;

	// Find all valid moves this player can make
	for (int row = 0; row < 3; row++)
	{
		for (int col = 0; col < 3; col++)
		{
			if (currentGame->gameBoard[row][col] == PlayerType::None)
			{
				possibleMoves[totalPossibleMoves++] = (row * 3) + col;
			}
		}
	}

	if (totalPossibleMoves != 0)
	{
		// There are valid moves left on the board, pick a random valid location
		int randomMoveIndex = currentPlayer->myRand() % totalPossibleMoves;

		int row = possibleMoves[randomMoveIndex] / 3;
		int col = possibleMoves[randomMoveIndex] % 3;
		currentGame->gameBoard[row][col] = currentPlayer->type;

		Log("Game %d: Player %d: Picked [Row: %d, Col: %d]\n", currentGame->gameNumber, currentPlayer->id, row, col);

		if (DidWeWin(row, col, currentGame, currentPlayer))
		{
			Log("Game %d:Player %d - Won\n", currentGame->gameNumber, currentPlayer->id);
			currentPlayer->winCount++;

			return GameState::Won;
		}
		else
		{
			return GameState::StillPlaying;
		}
	}

	// There are no more moves left, game resulted in a draw.
	Log("Game %d:Player %d - Draw\n", currentGame->gameNumber, currentPlayer->id);
	currentPlayer->drawCount++;

	return GameState::Draw;
}

// Play the entire game of Tic-Tac-Toe as 'currentPlayer' in 'currentGame'
void PlayGame(Player* currentPlayer, Game* currentGame)
{
	Log("Game %d:Player %d vs Player %d (Player %d) starting\n", currentGame->gameNumber, currentGame->playerX, currentGame->playerO, currentPlayer->id);

	if (currentGame->playerO == -1 || currentGame->playerX == -1)
	{
		Log("ERROR: Playing game with only one player present. Did you forget to wait for the second player in JoinGame()?\n");
		Pause();
		exit(1);
	}

	while (currentGame->currentGameState == GameState::StillPlaying)
	{
		if (currentGame->currentTurn != currentPlayer->type)
		{
			Log("ERROR: Wrong player is playing. You broke it.\n");
			Pause();
			exit(1);
		}

		currentGame->currentTurn = (currentPlayer->type == PlayerType::X) ? PlayerType::O : PlayerType::X;

		// Make a move on the game board
		currentGame->currentGameState = MakeAMove(currentPlayer, currentGame);
		PrintGameBoard(currentGame);

		switch (currentGame->currentGameState)
		{
		case GameState::StillPlaying:

			// The game is not over yet. 
			currentGame->gameCondition.notify_all();
			currentGame->gameCondition.wait(*currentGame->gameUniqueLock);

			continue;

		case GameState::Won:
			// We have won the game
			currentGame->gameCondition.notify_all();

			return;

		case GameState::Draw:

			// The game ended in a tie
			currentGame->gameCondition.notify_all();

			return;
		}
	}

	// Only one player will execute this logic. The winning/Tied player will exit this function
	//   upon finding out the game is over.
	if (currentGame->currentGameState == GameState::Won)
	{
		Log("Game %d:Player %d - Lost\n", currentGame->gameNumber, currentPlayer->id);
		(currentPlayer->loseCount)++;
	}
	else if (currentGame->currentGameState == GameState::Draw)
	{
		Log("Game %d:Player %d - Draw\n", currentGame->gameNumber, currentPlayer->id);
		(currentPlayer->drawCount)++; // count draw
	}
}

// Makes 'currentPlayer' join 'currentGame' and either waits for another player to
//  join or begins playing the game if both players are now present.
void JoinGame(Player* currentPlayer, Game* currentGame)
{
	// The player thread has joined a game and will begin playing it now.
	std::unique_lock<std::mutex> gameUniqueLock(currentGame->gameMutex);
	currentGame->gameUniqueLock = &gameUniqueLock;

	if (currentGame->playerO == -1)
	{
		Log("Player %d joining game %d as 'O'\n", currentPlayer->id, currentGame->gameNumber);

		currentGame->playerO = currentPlayer->id;
		currentPlayer->type = PlayerType::O;

		// Wait for other player to join the game
		currentGame->gameCondition.wait(gameUniqueLock, [&]
			{return currentGame->playerX != -1; });

	}
	else
	{
		Log("Player %d joining game %d as 'X'\n", currentPlayer->id, currentGame->gameNumber);

		currentGame->playerX = currentPlayer->id;
		currentPlayer->type = PlayerType::X;
	}

	PlayGame(currentPlayer, currentGame);
	currentGame->gameUniqueLock = nullptr;
	currentPlayer->gamesPlayed++;
	gameUniqueLock.unlock();
}
// Makes the specified player try to sequentially join and play each game in the
//   pool of games.
void TryToPlayEachGame(Player* currentPlayer)
{
	Log("Player %d starting to play games...\n", currentPlayer->id);

	Game* listOfGames = currentPlayer->gamePool->perGameData;
	int totalGameCount = currentPlayer->gamePool->totalGameCount;

	// All of our player threads will be going through the pool of games looking for the any
	//   games that aren't full. The player will join and play any non-full games it finds while
	//   iterating through the list of games.
	for (int i = 0; i < totalGameCount; i++)
	{
		// Checks to see if we can join this game
		listOfGames[i].playerCountMutex.lock();
		if (listOfGames[i].playerCount == 2)
		{
			// Game is full, skip it
			listOfGames[i].playerCountMutex.unlock();
			continue;
		}

		// This game wasn't full so we will join it
		listOfGames[i].playerCount++;
		listOfGames[i].playerCountMutex.unlock();

		// We joined the game so we can start playing it
		JoinGame(currentPlayer, &listOfGames[i]);
	}
}

// Entry point for player threads. 
void PlayerThreadEntrypoint(Player* currentPlayer)
{
	Log("Player %d waiting on starting gun\n", currentPlayer->id);

	// Let main know there's one more player thread running then wait for a notification from main.
	currentPlayer->playerPool->playersIncrementMutex.lock();
	currentPlayer->playerPool->totalPlayerCount++;
	currentPlayer->playerPool->playersIncrementMutex.unlock();
	currentPlayer->playerPool->playerCondition.notify_all();

	std::unique_lock<std::mutex> playerLock(currentPlayer->playerPool->playersIncrementMutex);
	currentPlayer->playerPool->playerIncrementCondition.wait(playerLock, [&]
		{ return currentPlayer->playerPool->gunFlag; });

	playerLock.unlock();

	// Attempt to play each game, all of the game logic will occur in this function
	Log("Player %d running\n", currentPlayer->id);
	TryToPlayEachGame(currentPlayer);

	// Let main know there's one less player thread running. 
	currentPlayer->playerPool->playersDecrementMutex.lock();
	currentPlayer->playerPool->totalPlayerCount--;
	currentPlayer->playerPool->playersDecrementMutex.unlock();
	currentPlayer->playerPool->playerCondition.notify_all();
}

// Displays the results of all players and all games to the console.
void PrintResults(const Player* perPlayerData, int totalPlayerCount, const Game* perGameData, int totalGameCount)
{
	int totalGamesWon = 0;
	int totalGamesTied = 0;
	int totalPlayerWins = 0;
	int totalPlayerLoses = 0;
	int totalPlayerTies = 0;

	Log("********* Player Results **********\n");
	for (int i = 0; i < totalPlayerCount; i++)
	{
		Log("Player %d, Played %d game(s), Won %d, Lost %d, Draw %d\n",
			perPlayerData[i].id,
			perPlayerData[i].gamesPlayed,
			perPlayerData[i].winCount,
			perPlayerData[i].loseCount,
			perPlayerData[i].drawCount
		);

		totalPlayerWins += perPlayerData[i].winCount;
		totalPlayerLoses += perPlayerData[i].loseCount;
		totalPlayerTies += perPlayerData[i].drawCount;
	}

	Log("Total Players %d, Wins %d, Losses %d, Draws %d\n\n\n", totalPlayerCount, totalPlayerWins, totalPlayerLoses, (totalPlayerTies / 2));

	Log("********* Game Results **********\n");
	for (int i = 0; i < totalGameCount; i++)
	{
		Log("Game %d - 'X' player %d, 'O' player %d, game result %s\n",
			perGameData[i].gameNumber,
			perGameData[i].playerX,
			perGameData[i].playerO,
			((perGameData[i].currentGameState == GameState::Won) ? "Won" : "Draw")
		);

		if (perGameData[i].currentGameState == GameState::Won)
		{
			totalGamesWon++;
		}
		else
		{
			totalGamesTied++;
		}
	}
	Log("Total Games = %d, %d Games Won, %d Games were a Draw\n\n\n", totalGameCount, totalGamesWon, totalGamesTied);
}

int main(int argc, char** argv)
{
	// Total number of games we're going to be playing.
	int totalGameCount;
	// Total number of players that will be playing.
	int totalPlayerCount;
	// An array of player specific data with exactly one entry for each player.
	Player* perPlayerData;
	// Contains all data needed to keep track of players.
	PlayerPool poolOfPlayers;
	// An array of game specific data with exactly one entry for each game.
	Game* perGameData;
	// Contains all of the games. 
	GamePool poolOfGames;

	std::cout << "Enter the number of players: ";
	std::cin >> totalPlayerCount;

	if (totalPlayerCount < 2)
	{
		std::cerr << "Error: Requires at least two players." << std::endl;
		Pause();
		return 1;
	}

	std::cout << "Enter the number of games: ";
	std::cin >> totalGameCount;

	if (totalGameCount < 0 || totalPlayerCount < 0)
	{
		std::cerr << "Error: All arguments must be positive integer values." << std::endl;
		Pause();
		return 1;
	}

	if (totalGameCount < 0 || totalPlayerCount < 0)
	{
		fprintf(stderr, "Error: All arguments must be positive integer values.\n");
		Pause();
		return 1;
	}

	if (totalPlayerCount < 2)
	{
		fprintf(stderr, "Error: Requires at least two players.\n");
		Pause();
		return 1;
	}

	Log("%s starting %d player(s) for %d game(s)\n", argv[0], totalPlayerCount, totalGameCount);

	// Allocate and array of players
	perPlayerData = new Player[totalPlayerCount];

	// Allocate array of games
	perGameData = new Game[totalGameCount];

	// Initialize pool of games
	poolOfGames.perGameData = perGameData;
	poolOfGames.totalGameCount = totalGameCount;

	// Initialize your data in the pool of players
	poolOfPlayers.totalPlayerCount = 0;
	poolOfPlayers.gunFlag = false;

	// Initialize each game
	for (int i = 0; i < totalGameCount; i++)
	{
		perGameData[i].playerO = -1;
		perGameData[i].playerX = -1;
		perGameData[i].gameNumber = i + 1;
		perGameData[i].currentTurn = PlayerType::X;
		perGameData[i].currentGameState = GameState::StillPlaying;
		perGameData[i].playerCount = 0;
		memset(perGameData[i].gameBoard, 0, sizeof(perGameData[i].gameBoard));
	}

	// Initialize each player
	for (int i = 0; i < totalPlayerCount; i++)
	{
		perPlayerData[i].id = i;
		perPlayerData[i].drawCount = 0;
		perPlayerData[i].gamesPlayed = 0;
		perPlayerData[i].loseCount = 0;
		perPlayerData[i].winCount = 0;
		perPlayerData[i].gamePool = &poolOfGames;
		perPlayerData[i].playerPool = &poolOfPlayers;
		perPlayerData[i].type = PlayerType::None;
		perPlayerData[i].myRand.Init(0, INT_MAX);
	}

	bool playAgain = true;

	while (playAgain) {
		// Start the player threads
		for (int i = 0; i < totalPlayerCount; i++) {
			std::thread(PlayerThreadEntrypoint, &perPlayerData[i]).detach();
		}

		// Wait for all players to be ready 
		std::unique_lock<std::mutex> totalPlayerCountUniqueLock(poolOfPlayers.totalPlayersMutex);
		poolOfPlayers.playerCondition.wait(totalPlayerCountUniqueLock, [&] {return poolOfPlayers.totalPlayerCount == totalPlayerCount; });

		poolOfPlayers.startingGunMutex.lock();
		poolOfPlayers.gunFlag = true;
		poolOfPlayers.startingGunMutex.unlock();

		// Notify all waiting threads that they can start playing.
		poolOfPlayers.playerIncrementCondition.notify_all();
		poolOfPlayers.playerDecrementCondition.notify_all();

		// Wait for all detached player threads to complete.
		poolOfPlayers.playerCondition.wait(totalPlayerCountUniqueLock, [&] {return poolOfPlayers.totalPlayerCount == 0; });

		PrintResults(perPlayerData, totalPlayerCount, perGameData, totalGameCount);

		// Ask the user if they want to play again
		char playAgainResponse;
		std::cout << "Do you want to play again? (y/n): ";
		std::cin >> playAgainResponse;

		playAgain = (playAgainResponse == 'y' || playAgainResponse == 'Y');

		// Cleanup
		LogSync(LogSyncOperation::Release);

		// Reset game state for the next round
		for (int i = 0; i < totalGameCount; i++) {
			perGameData[i].playerO = -1;
			perGameData[i].playerX = -1;
			perGameData[i].currentTurn = PlayerType::X;
			perGameData[i].currentGameState = GameState::StillPlaying;
			perGameData[i].playerCount = 0;
			memset(perGameData[i].gameBoard, 0, sizeof(perGameData[i].gameBoard));
		}

		for (int i = 0; i < totalPlayerCount; i++) {
			perPlayerData[i].gamesPlayed = 0;
			perPlayerData[i].winCount = 0;
			perPlayerData[i].loseCount = 0;
			perPlayerData[i].drawCount = 0;
			perPlayerData[i].type = PlayerType::None;
		}
	}

	// Cleanup and exit
	delete[] perGameData;
	delete[] perPlayerData;

	Pause();
	return 0;
}