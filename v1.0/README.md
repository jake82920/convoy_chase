# CONVOY CHASE

## FULL INSTALLATION GUIDE

0. This guide contains all steps for installation on Ubuntu 26.10 LTS
    - Feel free to use your preferred installation method
    - It works on Fedora too, if you're into that
1. Install GNU make
    - sudo apt install make
2. Clone the git repository
    - git clone https://github.com/jake82920/convoy_chase.git
3. Run the install script
    - sudo make install
4. Set passwords for each user
    - sudo passwd [username]

## RUNNING THE GAME

1. Start by running `./server` on user server
2. This will wait for each player to connect by executing `./player [1-4]`
    - I have been testing the game via ssh (which is activated in the install script)
