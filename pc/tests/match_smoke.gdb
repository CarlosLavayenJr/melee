# Run from repository root:
#   gdb -batch -x pc/tests/match_smoke.gdb build/phase2/melee_host.exe
#
# Drives the real menus into a VS match and captures a frame from the middle of
# it. The button presses and stick come from pc/src/pc_input_script.c
# (PC_INPUT_SCRIPT=vs) rather than from the debugger: a breakpoint on the pad
# read fires sixty times a second, and over the minutes this route takes that
# was slow enough that the test never finished. No scene is overridden and no
# state is forced -- every transition happens because the game's menu code
# acted on a PADStatus.
#
# The opening movie is skipped deliberately by pressing Start, and the debugger
# stops the test process once it has enough frames. Neither is a crash.
#
# Each scene the route passes through announces itself, so a FAIL says how far
# it got rather than only that it did not arrive.
set pagination off
set print thread-events off
set environment PC_CAPTURE_FRAME build/match-smoke.bmp
set environment PC_INPUT_SCRIPT vs
set $in_match = 0
set $match_frames = 0
set $reached_css = 0
set $reached_sss = 0
break main
run
# Disarm the capture so it cannot fire on the opening movie's first draw.
set capture_attempted = 1
delete 1
break __assert
break abort
break pc_sys_exit
break OSPanic

break mnCharSel_Scene_OnEnter
commands
silent
set $reached_css = 1
printf "TEST: character select entered\n"
continue
end

break mnStageSel_Scene_OnEnter
commands
silent
set $reached_sss = 1
printf "TEST: stage select entered\n"
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

continue
bt 12
printf "css=%d sss=%d in_match=%d match_frames=%d\n", $reached_css, $reached_sss, $in_match, $match_frames
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
