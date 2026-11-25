# The medal case: awards and promotions in play

`BF2.exe` 0x759e50 handles HUD events 0x26 (a rank) and 0x27 (a medal), and
shows one only when the event's player is the local one
(`RemoteWorld::awardNotices`, remote_world.h). The window is the HUD's own:

    hudBuilder.createTransformNode  IngameHud MedalArea 465 20 129 30
    hudBuilder.setNodeShowVariable  ShowRankUpIcon
    hudBuilder.setNodeLogicShowVariable OR ShowMedalIcon 1
    hudBuilder.createPictureNode    MedalArea MedalAreaBg 0 2 158 123   (awardWindow.tga)
    hudBuilder.createPictureNode    MedalArea ShowRankUpIcon 15 12 100 100  (RankUpIconPathString)
    hudBuilder.createPictureNode    MedalArea ShowMedalIcon 15 12 100 100   (MedalIconPathString)

(`Menu_client.zip`, HUD/HudSetup/Basic/HudElementsActionIcons.con, line 105 on;
the variables' fields are in hud-variables.md: +0x1b7, +0x1bc, +0x1c0.)

0x74f6b0 (medal) and 0x74f5b0 (rank): with a time above zero, the other kind's
show variable goes off, the icon's path goes into its string variable and its
own show variable comes on; when the time runs out both go off.
`hud::Manager::showAward`, timed in `animate`.

The icons:
* a promotion: `Ingame/GeneralIcons/Ranks/rank_%02i_Large.tga` (0x926738);
* a medal: `Ingame/GeneralIcons/Medals/AWARD_NAME_<id>.tga` at level 0 and
  `AWARD_NAME_<id>_<level>.tga` above. The HUD's textures carry those names
  and the server sends (id, level) the way `stats/medal_data.py` names its
  awards (`'<id>_<level>'`); the client's own table (0x7b4600, filled by
  `addAward`) is not read.

Ours: `app::takeAwards` (awards.h), from `updateLiveHud`. Test: `test_live_hud`.

Not measured:
* how long the window stays up: the time 0x759e50 hands 0x74f6b0 / 0x74f5b0
  is not read, and the stingers' lengths stand in (`Award.ogg` 3.804 s,
  `Promotion.ogg` 3.808 s, `Sound/GameSounds.con`);
* the stingers are not played, and the award's name is not put in the chat
  (the medal case's message, `vtbl[0x12c]`, is not read; a rank's name from
  0x4310d0 neither).
* Nothing here has been seen on screen: no award came during a check.
