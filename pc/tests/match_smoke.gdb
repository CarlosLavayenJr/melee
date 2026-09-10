# Run from repository root:
#   gdb -batch -x pc/tests/match_smoke.gdb build/phase2/melee_host.exe
#
# Lets the attract-mode demo run into a real VS match and captures a frame from
# the middle of it. No input is pressed and no scene is overridden: the demo is
# a match the game starts by itself, which is why this needs no navigation.
# The debugger stops the test process on purpose once it has enough frames --
# that is not a crash.
set pagination off
set print thread-events off
set environment PC_CAPTURE_FRAME build/match-smoke.bmp
set $in_match = 0
set $match_frames = 0
break main
run
# Disarm the capture so it does not fire on the opening movie's first draw.
set capture_attempted = 1
delete 1
break __assert
break abort
break pc_sys_exit
break OSPanic
break gm_Scene_Vs_OnEnter
commands
silent
set $in_match = 1
printf "TEST: VS scene entered\n"
continue
end
break gm_Scene_Vs_OnFrame
commands
silent
set $match_frames = $match_frames + 1
# Well past load and the countdown, so the capture shows a fight rather than
# an empty stage.
if $match_frames == 240
set capture_attempted = 0
end
if $match_frames < 400
continue
end
end
continue
bt 12
print $match_frames
if $match_frames < 400
printf "FAIL: stopped before 400 VS match frames\n"
frame 1
info args
info locals
quit 1
end
printf "PASS: 400 VS match frames; visual completeness requires capture review\n"
call pc_gx_material_report()
call pc_gx_texture_report()
quit 0
