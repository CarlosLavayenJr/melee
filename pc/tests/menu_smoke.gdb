# Run from repository root:
# gdb -batch -x pc/tests/menu_smoke.gdb build/phase2/melee_host.exe
# Deliberately skips the movie and presses Start through PADRead. No scene override.
# Stops after 240 menu frames or the first failure; writes only an ignored capture.
set pagination off
set print thread-events off
set environment PC_CAPTURE_FRAME build/menu-smoke.bmp
set $at_title = 0
set $at_menu = 0
set $title_polls = 0
set $menu_frames = 0
break main
run
set capture_attempted = 1
delete 1
break __assert
break abort
break pc_sys_exit
break gm_Scene_Title_OnEnter
commands
silent
set $at_title = 1
printf "TEST: title entered\n"
continue
end
break mnMain_Scene_OnEnter
commands
silent
set $at_menu = 1
printf "TEST: main-menu entry via Start\n"
continue
end
break mnMain_Scene_OnFrame
commands
silent
set $menu_frames = $menu_frames + 1
if $menu_frames == 120
set capture_attempted = 0
end
if $menu_frames < 240
continue
end
end
break pc/src/pc_pad.c:112
commands
silent
set status[0].button = 0
set status[0].stickX = 0
set status[0].stickY = 0
if !$at_menu
if $at_title
set $title_polls = $title_polls + 1
if ($title_polls % 60) >= 40 && ($title_polls % 60) < 44
set status[0].button = 0x1000
end
else
if (presented_frames >= 90 && presented_frames < 94) || (presented_frames >= 180 && presented_frames < 184) || (MoviePlayer.power && MoviePlayer.unk_78 >= 10 && MoviePlayer.unk_78 < 15)
set status[0].button = 0x100
end
end
end
continue
end
continue
bt 16
print $menu_frames
print mn_804A04F0.cur_menu
print mn_804A04F0.hovered_selection
if $menu_frames < 240
printf "FAIL: stopped before 240 main-menu frames\n"
frame 1
info args
info locals
quit 1
end
printf "PASS: 240 main-menu frames; visual completeness requires capture review\n"
quit 0
