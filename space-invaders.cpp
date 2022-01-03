// clang++ -std=c++20 -Wall -Werror -Wextra -Wpedantic -g3 -o team10-spaceinvaders team10-spaceinvaders.cpp

// Works best in Visual Studio Code if you set:
//   Settings -> Features -> Terminal -> Local Echo Latency Threshold = -1

#include<iostream>
#include<termios.h>
#include<vector>
#include<string>
#include<cmath>
#include<chrono>
#include<fcntl.h> 
#include<thread> //for space invaders intro

using namespace std;
using namespace this_thread; //for space invaders intro

// Types

struct termios initialTerm; // declaring variable of type "struct termios" named initialTerm
struct position { unsigned int row; unsigned int col; };
typedef struct position positionstruct;
typedef vector< string > stringvector;

// Alien Types
struct alien {
    int hp {1};
    float speed {1.0};
    bool isEmpty {false};
    int alienRow {-1};
    int alienCol {-1};
    char displayChar {' '};
};

// Laser Types

struct laser {
    position position;
    unsigned int colour;
};

typedef vector< laser > laservector;
typedef vector< alien > alienvector;
typedef vector< alienvector > alienmatrix;

// Constants

// Disable JUST this warning (in case students choose not to use some of these constants)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"

//const char START_CHAR { 'w' };
const char SHOOT_CHAR  { 's' };
const char LEFT_CHAR  { 'a' };
const char RIGHT_CHAR { 'd' };
const char QUIT_CHAR  { 'q' };
const int TERMINAL_WIDTH {100};
const int TERMINAL_HEIGHT {30};
// https://en.wikipedia.org/wiki/ANSI_escape_code#3-bit_and_4-bit
const string ANSI_START { "\033[" };
const string START_COLOUR_PREFIX {"1;"}; 
const string START_COLOUR_SUFFIX {"m"};
const string STOP_COLOUR  {"\033[0m"};
const string GREEN {"\033[1;32m"};
const string RED{"\033[1;31m"};
const unsigned int COLOUR_IGNORE { 0 }; // this is a little dangerous but should work out OK
const unsigned int COLOUR_WHITE  { 37 };
const unsigned int COLOUR_RED    { 31 };
const unsigned int COLOUR_BLUE   { 34 };
const unsigned int COLOUR_BLACK  { 30 };
const unsigned int COLOUR_BRIGHTGREEN  { 92 };
//const stringvector for display of the cannon
const stringvector CANNON_SPRITE { // 2 * 6
    {" _/ \\_ "},
    {"|_____|"},
};

// State Variables
int score {0};
int alienStepsTaken {0};
int kills {0};
int counter {5};
// Boolean for winning the game
bool win {false};

// Global Variables
alienmatrix screenMatrix;
alienmatrix alienMatrix;

#pragma clang diagnostic pop

// DEMO CODE STARTS -------------------------------------------------------------------------------------------------------------------------------
// Utilty Functions

// These two functions are taken from Stack Exchange and are 
// all of the "magic" in this code.
auto SetupScreenAndInput() -> void
{
    struct termios newTerm;
    // Load the current terminal attributes for STDIN and store them in a global
    tcgetattr(fileno(stdin), &initialTerm);
    newTerm = initialTerm;
    // Mask out terminal echo and enable "noncanonical mode"
    // " ... input is available immediately (without the user having to type 
    // a line-delimiter character), no input processing is performed ..."
    newTerm.c_lflag &= ~ICANON;
    newTerm.c_lflag &= ~ECHO;
    // Set the terminal attributes for STDIN immediately
    tcsetattr(fileno(stdin), TCSANOW, &newTerm);
}
auto TeardownScreenAndInput() -> void
{
    // Reset STDIO to its original settings
    tcsetattr(fileno(stdin), TCSANOW, &initialTerm);
}

auto SetNonblockingReadState( bool desiredState = true ) -> void
{
    auto currentFlags { fcntl( 0, F_GETFL ) };
    if ( desiredState ) { fcntl( 0, F_SETFL, ( currentFlags | O_NONBLOCK ) ); }
    else { fcntl( 0, F_SETFL, ( currentFlags & ( ~O_NONBLOCK ) ) ); }
    cerr << "SetNonblockingReadState [" << desiredState << "]" << endl;
}

// Everything from here on is based on ANSI codes
auto ClearScreen() -> void { cout << ANSI_START << "2J" ; }
auto MoveTo( unsigned int x, unsigned int y ) -> void { cout << ANSI_START << x << ";" << y << "H" ; }
auto HideCursor() -> void { cout << ANSI_START << "?25l" ; }
auto ShowCursor() -> void { cout << ANSI_START << "?25h" ; }
auto GetTerminalSize() -> position
{
    // This feels sketchy but is actually about the only way to make this work
    MoveTo(999,999);
    cout << ANSI_START << "6n" ; // ask for Device Status Report 
    string responseString;
    char currentChar { static_cast<char>( getchar() ) };
    while ( currentChar != 'R')
    {
        responseString += currentChar;
        currentChar = getchar();
    }
    // format is ESC[nnn;mmm ... so remove the first 2 characters + split on ; + convert to unsigned int
    // cerr << responseString << endl;
    responseString.erase(0,2);
    // cerr << responseString << endl;
    auto semicolonLocation = responseString.find(";");
    // cerr << "[" << semicolonLocation << "]" << endl;
    auto rowsString { responseString.substr( 0, semicolonLocation ) };
    auto colsString { responseString.substr( ( semicolonLocation + 1 ), responseString.size() ) };
    // cerr << "[" << rowsString << "][" << colsString << "]" << endl;
    auto rows = stoul( rowsString );
    auto cols = stoul( colsString );
    position returnSize { static_cast<unsigned int>(rows), static_cast<unsigned int>(cols) };
    // cerr << "[" << returnSize.row << "," << returnSize.col << "]" << endl;
    return returnSize;
}

// This is pretty sketchy since it's not handling the graphical state very well or flexibly
auto MakeColour( string inputString, 
                 const unsigned int foregroundColour = COLOUR_WHITE,
                 const unsigned int backgroundColour = COLOUR_IGNORE ) -> string
{
    string outputString;
    outputString += ANSI_START;
    outputString += START_COLOUR_PREFIX;
    outputString += to_string( foregroundColour );
    if ( backgroundColour ) { 
        outputString += ";";
        outputString += to_string( ( backgroundColour + 10 ) ); // Tacky but works
    }
    outputString += START_COLOUR_SUFFIX;
    outputString += inputString;
    outputString += STOP_COLOUR;
    return outputString;
}

// This is super sketchy since it doesn't do (e.g.) background removal
// or allow individual colour control of the output elements.
auto DrawSprite( position targetPosition,
                 stringvector sprite,
                 const unsigned int foregroundColour = COLOUR_WHITE,
                 const unsigned int backgroundColour = COLOUR_IGNORE)
{
    MoveTo( targetPosition.row, targetPosition.col );
    for ( auto currentSpriteRow = 0 ;
                currentSpriteRow < static_cast<int>(sprite.size()) ;
                currentSpriteRow++ )
    {
        cout << MakeColour( sprite[currentSpriteRow], foregroundColour, backgroundColour );
        MoveTo( ( targetPosition.row + ( currentSpriteRow + 1 ) ) , targetPosition.col );
    };
}
// DEMO CODE ENDS -------------------------------------------------------------------------------------------------------------------------------



// Starter screen function
auto Instructions() -> void{
    cout << GREEN << "                            ████████████      ████████████      ████████████      ████████████      ████████████" << endl;
    sleep_for(0.1s); //sleep_for blocks the execution of the next command for the specified time
    cout << "                            ████████████      ████████████      ████████████      ████████████      ████████████" << endl;
    sleep_for(0.1s);
    cout << "                            ████              ████    ████      ████    ████      ████              ████" << endl;
    sleep_for(0.1s);
    cout << "                            ████              ████    ████      ████    ████      ████              ████" << endl;
    sleep_for(0.1s);
    cout << "                            ████████████      ████    ████      ████    ████      ████              ████████████" << endl;
    sleep_for(0.1s);
    cout << "                            ████████████      ████████████      ████████████      ████              ████████████" << endl;
    sleep_for(0.1s);
    cout << "                                    ████      ████              ████    ████      ████              ████" << endl;
    sleep_for(0.1s);
    cout << "                                    ████      ████              ████    ████      ████              ████" << endl;
    sleep_for(0.1s);
    cout << "                            ████████████      ████              ████    ████      ████████████      ████████████" << endl;
    sleep_for(0.1s);
    cout << "                            ████████████      ████              ████    ████      ████████████      ████████████" << endl;
    sleep_for(0.1s);
    cout << " " << endl;
    sleep_for(0.1s);
    cout << " " << endl;
    sleep_for(0.1s);
    cout << "████      ████████████      ████                ████      ████████████      ██████████           ████████████      ████████████      ████████████" << endl;
    sleep_for(0.1s);
    cout << "████      ████████████       ████              ████       ████████████      ████████████         ████████████      ████████████      ████████████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████        ████            ████        ████    ████      ████     ████        ████              ████    ████      ████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████         ████          ████         ████    ████      ████      ████       ████              ████    ████      ████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████          ████        ████          ████    ████      ████       ████      ████████████      ████    ████      ████████████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████           ████      ████           ████████████      ████       ████      ████████████      ████████████      ████████████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████            ████    ████            ████    ████      ████      ████       ████              ████████                  ████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████             ████  ████             ████    ████      ████     ████        ████              ████ ████                 ████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████              ████████              ████    ████      ████████████         ████████████      ████   ████       ████████████" << endl;
    sleep_for(0.1s);
    cout << "████      ████    ████                ████                ████    ████      ██████████           ████████████      ████    ████      ████████████" << endl;
    cout << " " << endl;
    cout << " " << endl;
    cout << "Instructions: press 's' to shoot \nPress 'a' and 'd' to move left and right \nPress 'q' to quit the game" << endl;
    cout << "Press any key to start the game!" << endl;
    sleep_for(5s);
}

// Alien Functions

auto GenerateAlienMatrix() -> void
{
    for (int i = 0; i < 3; i++) {
        alienMatrix.push_back(vector<alien>());
        for (int j = 0; j < 5; j++) {
            alien newAlien {
                .alienRow = i,
                .alienCol = j
            };
            alienMatrix[i].push_back(newAlien);
        }
    }
}

auto GenerateMatrix() -> void
{

    int isAlienTracker = 1; // Checks whether each column contains a part of an alien or not
    int alienTopBottomTracker = 1; // Tracks whether or not the current row contains the top half of the alien, or the bottom half.
    int alienNum = 1; // Tracks how many aliens have already been added for each row
    float alienRowsUsed = 1; // Trakcs how many rows of alien have already been added
    int alienIDRow = 0; // Which row from alienMatrix to pull from
    int alienRowTracker = 0; // 1 = Top row, 2 = Bottom Row, 3 = Empty row, initialized at 0 since the first row will always be empty

    // Empty alien struct to fill out the rest of the alienmatrix
    alien emptyAlien {
        .hp = 0,
        .isEmpty = true,
        .alienRow = 0,
        .alienCol = 0,
        .displayChar = ' '
    };

    // Always will be an empty line at the very top of the screen, but can be more as the aliens move towards the bottom
    for (int i = 0; i < alienStepsTaken; i++) {
        screenMatrix.push_back(vector<alien>());
        for (int j = 0; j < 100; j++) {
                screenMatrix[i].push_back(emptyAlien);
            }
    }

    for ( int i = alienStepsTaken; i < 30; i++) { // For-loop starts at alienStepsTaken so there are the correct number of lines
        screenMatrix.push_back(vector<alien>());
        
        // Adds the padding on the left of the first alien
        if ((alienRowTracker == 1 || alienRowTracker == 2) && alienRowsUsed < 4) {
            for (int j = 0; j < 10; j++) {
                screenMatrix[i].push_back(emptyAlien);
                
            }
        }

        for ( int j = 10; j < 100; j++ ) { // Starts at j = 10 since the padding has already added 10 spaces
            if ( (alienRowTracker == 1 || alienRowTracker == 2) && alienRowsUsed < 4 && alienNum <= 5) {
                if (alienTopBottomTracker == 1) { // Top Row Alien looks like "\      /" -> only the first character and eigth character are actual alien parts
                     if (isAlienTracker == 1) {
                        screenMatrix[i].push_back(alienMatrix[alienIDRow][alienNum - 1]);
                        screenMatrix[i][j].displayChar = '\\';
                    } else if (isAlienTracker == 8) {
                        screenMatrix[i].push_back(alienMatrix[alienIDRow][alienNum - 1]);
                        screenMatrix[i][j].displayChar = '/';
                    } else if (isAlienTracker <= 7 && isAlienTracker > 1) {
                        screenMatrix[i].push_back(emptyAlien);
                    } 
                } else if (alienTopBottomTracker == 2) { // Second Row Alien looks like " |_  _|" -> character 2-7 are all characters, symmetrical about the middle
                    if (isAlienTracker == 1 || isAlienTracker == 8)  {
                        screenMatrix[i].push_back(emptyAlien);
                    } else if (isAlienTracker == 2 || isAlienTracker == 7) {
                        screenMatrix[i].push_back(alienMatrix[alienIDRow][alienNum - 1]);
                        screenMatrix[i][j].displayChar = '|';
                    } else if (isAlienTracker == 3 || isAlienTracker == 6) {
                        screenMatrix[i].push_back(alienMatrix[alienIDRow][alienNum - 1]);
                        screenMatrix[i][j].displayChar = '_';
                    } else if (isAlienTracker == 4 || isAlienTracker == 5) {
                        screenMatrix[i].push_back(alienMatrix[alienIDRow][alienNum - 1]);
                        screenMatrix[i][j].displayChar = ' ';
                    }
                }

                // 10 Empty Spaces between Each Alien
                if (isAlienTracker >= 9 && isAlienTracker <= 17) {
                    screenMatrix[i].push_back(emptyAlien);
                } else if (isAlienTracker == 18) { // On last space, reset isAlienTracker for new alien, then increment number of aliens in the current row
                    screenMatrix[i].push_back(emptyAlien);
                    isAlienTracker = 0;
                    alienNum += 1;
                }

                // If the alien is dead, then don't display it's characters
                if (screenMatrix[i][j].hp == 0) {
                    screenMatrix[i][j].displayChar = ' ';
                }

                isAlienTracker += 1;
            } else { // Empty line, no aliens
                screenMatrix[i].push_back(emptyAlien);
            }

        }

        // Keeps track of how many alien rows have been used
        if ( (alienRowTracker == 1 || alienRowTracker == 2)) {
            alienRowsUsed += 0.5; // 0.5 rows * 2 rows per alien = increments by 1 per 2 rows of aliens.

            // Alternates top half of alien with bottom half of alien
            if (alienTopBottomTracker == 1) {
                alienTopBottomTracker = 2;
            } else {
                alienTopBottomTracker = 1;
                alienIDRow += 1;
            }
        }

        // Keeps rotating between empty line, top alien, and bottom alien.
        if (alienRowTracker != 3) {
            alienRowTracker += 1;
        } else {
            alienRowTracker = 1;
        }

        // Resets variables for next row
        alienNum = 1;
        isAlienTracker = 1;
    }  
}

// Displays the aliens on the screen
auto DisplayMatrix() -> void
{
    for ( int i = 0; i < TERMINAL_HEIGHT; i++) {
        for ( int j = 0; j < TERMINAL_WIDTH; j++) {
            cout << screenMatrix[i][j].displayChar;
        }
        cout << endl;
    }
} 

// Laser Functions

auto CreateLaser (laservector & lasers, unsigned int currentRow = TERMINAL_HEIGHT - 2, unsigned int currentCol = TERMINAL_WIDTH / 2) -> void {
    laser newLaser {
        .position = {.row = currentRow, .col = currentCol + 3},
        .colour = COLOUR_BRIGHTGREEN 
    };
    lasers.push_back( newLaser );
}

auto UpdateLaserPositions ( laservector & lasers ) -> void {
    for ( auto & currentLaser : lasers) { //modern c++: ranged for loop and auto :D
        int testPosition = currentLaser.position.row - 1;
        currentLaser.position.row = max( 0, min ( TERMINAL_HEIGHT, testPosition ));
    }
}

auto DrawLasers( laservector & lasers ) -> void {
    int laserLength = lasers.size();
    for ( int i = 0; i < laserLength; i++ ) {
        if (lasers[i].position.row > 1) { // Don't print lasers that are out of bounds
            MoveTo(lasers[i].position.row, lasers[i].position.col);
            cout << MakeColour("|", lasers[i].colour) << flush;
        }
        else 
            lasers.erase(lasers.begin() + i); // Remove lasers that are out of bounds
    }
}

//Function checks for when the laser hits the aliens. If the alien and laser are in the same position then it deletes the alien
//Function also keeps track of the score by incrementing the score by 10 everytime an alien is killed
//Function also keeps track of the number of aliens killed. If all the aliens are killed then it sets the win boolean to true
auto CheckAliens(laservector & lasers) -> void {
    int laserLength = lasers.size();
    for ( int i = 0; i < laserLength; i++ ){
        laser currentLaser = lasers[i];
        int laserRow = currentLaser.position.row;
        int laserCol = currentLaser.position.col - 1;
        if (not screenMatrix[laserRow][laserCol].isEmpty || screenMatrix[laserRow][laserCol].hp > 0) { 
            alienMatrix[screenMatrix[laserRow][laserCol].alienRow][screenMatrix[laserRow][laserCol].alienCol].hp = 0;
            alienMatrix[screenMatrix[laserRow][laserCol].alienRow][screenMatrix[laserRow][laserCol].alienCol].isEmpty = true;
            
            score += 10;
            kills += 1;
            
            lasers.erase(lasers.begin() + i); // Remove the laser that hit an alien    
        }
    }
    if (kills == 15){
        win = true;
    }
}

auto CheckGameOver() -> bool
{
    int frontRow; // How far ahead the alien at the very front is
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 5; j++) {
            if (i == 2 && alienMatrix[i][j].hp > 0) {
                frontRow = 8;
                break;
            } else if ( i == 1 && alienMatrix[i][j].hp > 0) {
                frontRow = 5;
                break;
            } else if ( i == 0 && alienMatrix[i][j].hp > 0) {
                frontRow = 2;
                break;
            } 
        }
    }

    if (frontRow + alienStepsTaken == 28) { // There are 30 lines, line 29 and 30 are where the spaceship is
        return true; // Game Over
    } else {
        return false;
    }
}

auto main() -> int
{
    // 0. Set Up the system and get the size of the screen

    SetupScreenAndInput();
    const position TERMINAL_SIZE { GetTerminalSize() };
    if ( ( TERMINAL_SIZE.row < 30 ) or ( TERMINAL_SIZE.col < 100 ) )
    {
        ShowCursor();
        TeardownScreenAndInput();
        cout << endl <<  "Terminal window must be at least " << TERMINAL_HEIGHT<< " by " << TERMINAL_WIDTH << " to run this game" << endl;
        return EXIT_FAILURE;
    }

    // 1. Initialize State
    position currentPosition {TERMINAL_HEIGHT,TERMINAL_WIDTH/2};
    laservector lasers {};
    unsigned int ticks {0};

    bool allowBackgroundProcessing = true;

    // Timer Variables
    auto startTimestamp { chrono::steady_clock::now() };
    auto endTimestamp { startTimestamp };
    int elapsedTimePerTick { 100 }; // Every 0.1s check on things
    int alienMoveTimer { 0 };
    GenerateAlienMatrix();
    SetNonblockingReadState(allowBackgroundProcessing);

    // GameLoop
    char currentChar {'z'}; // I would rather use a different approach, but this is quick
        ClearScreen();
        HideCursor(); 
        Instructions();

    if (getchar()) {
        cout << "Game will start in..." << endl; 
        while ( counter != 0){
            cout << counter << endl;
            sleep_for(1s);
            counter--;
        }
    }

    while( currentChar != QUIT_CHAR )
    {
        endTimestamp = chrono::steady_clock::now();
        auto elapsed { chrono::duration_cast<chrono::milliseconds>( endTimestamp - startTimestamp ).count() };
        
        // We want to process input and update the world enough time has elapsed
        if (elapsed >= elapsedTimePerTick) {
            ticks += 1;

            // 2. Update State
            if ( currentChar == LEFT_CHAR )  { currentPosition.col = max(  1U,(currentPosition.col - 1) ); }
            if ( currentChar == RIGHT_CHAR ) { currentPosition.col = min( 100U,(currentPosition.col + 1) ); }
            if ( currentChar == SHOOT_CHAR ) {
                CreateLaser( lasers, currentPosition.row, currentPosition.col );
            }

            // 3. Update Screen

            // 3.A Prepare Screen
            ClearScreen();
            HideCursor(); // sometimes the Visual Studio Code terminal seems to forget

            // 3.B Draw based on state
            MoveTo( 0, 0 );
            string currentScore = to_string(score);
            int currentScoreLength = currentScore.length();
            MoveTo( 0, TERMINAL_WIDTH - currentScoreLength ); // Aligns the text with the top right corner
            cout << "SCORE: " << RED << currentScore << STOP_COLOUR;
            UpdateLaserPositions( lasers );
            CheckAliens( lasers );
            DrawLasers( lasers );
            if (win == true){ //If the player wins then it breaks out of the while loop
                break;
            }

            if (alienMoveTimer == 20) { 
                alienStepsTaken += 1; 
                alienMoveTimer = 0;
            } else {
                alienMoveTimer += 1;
            }
            screenMatrix.clear();
            GenerateMatrix();
            MoveTo( 2, 0 ); // Leaves space for the score tracker at the top of the terminal
            DisplayMatrix();
            
            MoveTo( currentPosition.row, currentPosition.col );
            DrawSprite( {currentPosition.row, currentPosition.col }, CANNON_SPRITE );

            bool gameover = CheckGameOver();
            if (gameover) {
                ClearScreen();
                HideCursor();
                cout << "\nGame Over! \nYou got a high score of: " << RED << to_string(score) << STOP_COLOUR;
                break;
            }

            // 4. Prepare for the next pass
            currentChar = getchar();
            startTimestamp = endTimestamp;
            HideCursor();
        }
        
    }
    //If user killed all the aliens then it clears the screen and prints out a win statement
    if (win == true){
        ClearScreen();
        HideCursor();
        cout << "\nCongrats! You won! \nYou got a final score of: " << RED << to_string(score) << STOP_COLOUR;
    }
    // N. Tidy Up and Close Down
    ShowCursor();
    TeardownScreenAndInput();
    cout << endl; // be nice to the next command
    return EXIT_SUCCESS;
}