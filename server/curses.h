#include "minorGems/util/SimpleVector.h"


void initCurses();

void freeCurses();


void cursesLogBirth( char *inEmail );
void cursesLogDeath( char *inEmail );



// check whether a player has a curse token
// meant to be called at birth
char hasCurseToken( char *inEmail );


// gets a list of email addresses for players who now have curse tokens when
// they didn't the last time hasCurseToken was called
//
// Passed-in vector is filled with emails that are destroyed by caller
void getNewCurseTokenHolders( SimpleVector<char*> *inEmailList );



// returns true of curse effective
char cursePlayer( char *inGiverEmail, char *inReceiverName );

void logPlayerNameForCurses( char *inPlayerEmail, char *inPlayerName );



// returns curse level, or 0 if not cursed
int getCurseLevel( char *inPlayerEmail );
