# Run from repository root:
#   gdb -batch -x pc/tests/match_smoke.gdb build/phase2/melee_host.exe
#
# Drives the real menus with real PADStatus input, from the title through
# VS Mode, character select and stage select, into a match, then captures a
# frame from the middle of it. No scene is overridden and no state is forced:
# every transition happens because the game's own menu code acted on a button.
#
# This exists because the attract-mode demo picks a different route on every
# run -- it has been seen going to a VS match, character select, the main
# menu, an event menu and tournament mode, each failing somewhere different.
# Driving the menus makes the route the same every time.
#
# The movie is skipped deliberately, and the debugger stops the test process
# once it has enough frames. Neither is a crash.
set pagination off
set print thread-events off
set environment PC_CAPTURE_FRAME build/match-smoke.bmp
set $at_title = 0
set $title_polls = 0
set $menu_polls = 0
set $css_polls = 0
set $sss_polls = 0
set $in_match = 0
set $match_frames = 0
break main
run
# Disarm the capture so it cannot fire on the opening movie's first draw.
set capture_attempted = 1
delete 1
break __assert
break abort
break pc_sys_exit
break OSPanic

break gm_Scene_Title_OnEnter
commands
silent
set $at_title = 1
printf "TEST: title entered\n"
continue
end

break gm_Scene_Vs_OnEnter
commands
silent
set $in_match = 1
printf "TEST: VS match entered\n"
continue
end

break gm_Scene_Vs_OnFrame
commands
silent
set $match_frames = $match_frames + 1
# Well past the countdown, so the capture shows a fight and not an empty stage.
if $match_frames == 240
set capture_attempted = 0
end
if $match_frames < 400
continue
end
end

# One injection point for every button, driven by which scene is live rather
# than by frame counts, so a slow load cannot desynchronise the script.
break pc/src/pc_pad.c:112
commands
silent
set status[0].button = 0
set status[0].stickX = 0
set status[0].stickY = 0
if !$in_match
  if $at_title && mn_804A04F0.cur_menu == 0 && $title_polls < 400
    # Title: Start to reach the main menu.
    set $title_polls = $title_polls + 1
    if ($title_polls % 60) >= 40 && ($title_polls % 60) < 44
      set status[0].button = 0x1000
    end
  else
    if mn_804A04F0.cur_menu == 0 && $at_title
      set $menu_polls = $menu_polls + 1
    end
  end
  # Main menu: move to VS Mode (selection 1) and confirm.
  if mn_804A04F0.cur_menu == 0 && $menu_polls > 0
    if ($menu_polls % 40) >= 20 && ($menu_polls % 40) < 24
      if mn_804A04F0.hovered_selection != 1
        set status[0].button = 0x0004
      else
        set status[0].button = 0x0100
      end
    end
  end
  # VS submenu: confirm the first entry, Melee.
  if mn_804A04F0.cur_menu == 2
    set $menu_polls = $menu_polls + 1
    if ($menu_polls % 40) >= 20 && ($menu_polls % 40) < 24
      set status[0].button = 0x0100
    end
  end
end
continue
end

break mnCharSel_Scene_OnFrame
commands
silent
set $css_polls = $css_polls + 1
continue
end

break mnStageSel_Scene_OnFrame
commands
silent
set $sss_polls = $sss_polls + 1
continue
end

continue
bt 12
printf "title_polls=%d menu_polls=%d css_polls=%d sss_polls=%d match_frames=%d\n", $title_polls, $menu_polls, $css_polls, $sss_polls, $match_frames
print mn_804A04F0.cur_menu
if $match_frames < 400
printf "FAIL: stopped before 400 VS match frames\n"
frame 1
info args
quit 1
end
printf "PASS: 400 VS match frames; visual completeness requires capture review\n"
call pc_gx_material_report()
call pc_gx_texture_report()
quit 0
