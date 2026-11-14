# CONVOY CHASE

## FULL INSTALLATION GUIDE

0. This guide contains all steps for installation on Ubuntu 26 LTS
    - Feel free to use your preferred installation method
    - It works on Fedora too, if you're into that
1. Install GNU make
    - sudo apt install make
2. Clone the git repository
    - git clone https://github.com/jake82920/convoy_chase.git
3. Run the install script from the directory containing the desired version
    - sudo make install
4. Set passwords for each user
    - sudo passwd [username]

## RUNNING THE GAME

1. This is a terminal based game. Any method of connecting to a terminal as a user should work.
    - In my case, this was a combination of using `ssh` to connect the machine, and `su - [username]` to connect to each user in a tmux pane.
    - *NOTE: the ssh server is enabled in the install script*
2. Within the home directory of user 'server', run `./server` to begin waiting for users to connect
2. Within the home directory of each player, run `./player [1-4]`
    - Upon connection of each player, the game sequence will begin

## PLAYING THE GAME: A CONVOY ON THE HIGH SEAS

A Royal Navy convoy carrying valuable cargo is racing toward port. But one Captain is secretly a **PIRATE INFILTRATOR**, working to delay the fleet until pursuing pirates can catch it.

### HOW TO WIN

**ROYAL NAVY:** Advance the convoy 4 times and reach port.

**PIRATE:** Cause 4 delays before the convoy reaches port.

## HOW A ROUND WORKS

One Captain becomes Commodore and proposes an action.

### SAIL

The Commodore selects 2 or 3 Captains for a mission.

Selected Captains secretly commit manpower.

The mission needs 6 total manpower to succeed.

Success advances the convoy; failure advances the pirates.

### ANCHOR

The fleet stops so exhausted crews can rest.

Every Captain recovers 3 manpower, up to a maximum of 6.

But the delay also advances the pirates.

## VOTING AND STRATEGY

Every proposal must be approved by the fleet.

A tie is a rejection. Three rejected or missed proposals also advance the pirates.

Manpower is limited. Watch who contributes, who pushes to anchor, and which missions mysteriously fail.

Discuss, accuse, bluff, and cooperate.

One of you is working against the fleet.

**Trust carefully.**
