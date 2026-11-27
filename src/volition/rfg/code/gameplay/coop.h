// OpenRFG (new code, not in rfg.exe): single player co-op over direct IP / LAN.
// Implementation: volition/rfg/code/gameplay/coop.cpp
//
// One player hosts their single player game; up to three others join it by IP address. Each machine runs its own
// world and streams it around its own player, so players can be in different sectors at once. What is shared is the
// players themselves: every machine shows each other player as a second player character (the multiplayer character
// they picked, or Mason), moving, aiming, crouching, jumping and firing as the real one does, whenever that player is
// inside the part of the world this machine has loaded. NPCs, traffic, destruction and missions are not shared yet.
//
// Joining is only allowed while the host is in free roam (no mission, activity or cutscene). A guest that connects is
// moved next to the host once.
//
// The main menu's Co-op entry picks the role (coop_start_host / coop_start_join) and then loads or starts a game as
// usual; gameplay_do_frame calls coop_do_frame. The join address can be typed in the Co-op box (saved to config.ini);
// the address and the port are in config.ini, section [coop] (join_address, port), read and written by
// game_options.cpp. Any network that gives the players IPv4 addresses that reach each other works: a LAN, a forwarded
// port, or a virtual LAN (Radmin VPN, Hamachi).
#pragma once

enum coop_mode
{
    COOP_MODE_OFF = 0,
    COOP_MODE_HOST = 1,
    COOP_MODE_JOIN = 2,
};

// config.ini [coop]: the host to join ("a.b.c.d" or "a.b.c.d:port") and the UDP port to host on.
extern char Coop_join_address[64];
extern int Coop_port;

// Listens on Coop_port for guests. False when the port cannot be opened (the reason is in the log).
bool coop_start_host();
// Connects to Coop_join_address once a game is running. False when the address is not valid.
bool coop_start_join();
// Leaves the session (guests are told) and removes the other players' characters.
void coop_stop();
coop_mode coop_get_mode();
// True with no mission, activity or cutscene running: the only time the host lets guests in.
bool coop_is_free_roam();
// Once per gameplay frame (gameplay_do_frame).
void coop_do_frame();
// The text the main menu shows for the join address (Coop_join_address with the port).
void coop_get_join_target_text(char* buffer, int buffer_size);
// True when the text is "a.b.c.d" or "a.b.c.d:port" (what the main menu's address box accepts).
bool coop_is_valid_join_address(const char* text);
// This machine's IPv4 addresses, comma separated, loopback left out (virtual LAN adapters such as Radmin VPN or
// Hamachi included): shown to the host to pass on.
void coop_get_local_addresses(char* buffer, int buffer_size);
// The clipboard's text, for pasting an address with Ctrl+V in the address box.
void coop_get_clipboard_text(char* buffer, int buffer_size);
// control_gameplay.cpp: the local player jumped (0) or swung (1 primary, 2 secondary, 3 tertiary, 4 firearm butt).
void coop_note_local_action(int action);
