# PS3 test checklist

This checklist applies to the new seven-runner test build. A successful build does not establish that every core plays correctly.

1. Install the new test PKG and open Atari4PS3.
2. Navigate every blade, including wrapping from Settings to Atari 2600. Confirm controller input remains responsive while idle and during repeated navigation.
3. Check the system menu and quit from the launcher, a running game and the Jaguar warning screen. A quit on the warning screen must not start a game.
4. Test one supplied game per system. Check picture size, audio, controls, Select+Start return and repeated launch/return.
5. Check that 8-bit computer and 5200 saves stay in their separate folders. Reopen a saved game to confirm persistence.
6. Confirm the ST blade gives a clear missing-BIOS message without `tos.img`.

Logs are `/dev_hdd0/atari/logs/m0.log` and `watchdog.log`. Note the selected system and the last action if a problem occurs. This test build has not yet passed this checklist.
