# Run from repository root:
#   gdb -batch -x pc/tests/sss_geometry_probe.gdb build/phase2/melee_host.exe
#
# Prints the stage-select screen's geometry, so a scripted route can be aimed
# at a named stage instead of pressing Start on whatever the cursor opened on.
#
# The screen works the same way character select does, which is what makes an
# open-loop route possible. fn_8025A560 moves the cursor by 0.03 per stick
# unit per frame and clamps it to x in [-27, 27] and y in [-19, 19], so
# holding a direction long enough parks it on a known edge no matter where it
# started; from there each frame of full deflection (stick 80) moves it
# exactly 2.4 units. The slot under the cursor is then decided by hit-testing
# the cursor against each entry's jobj position with half-extents xC and x10.
#
# Those positions are loaded from the disc, so they have to be measured rather
# than read out of the initialisers -- mnStageSel_803F06D0's static half holds
# only flags, ids and extents. What this prints is the missing half.
#
# .xB is the StKind the slot selects (mnStageSel_Scene_OnExit assigns it
# straight to rules.stkind), so the row with xB == 3 is Pokemon Stadium.
set pagination off
set print thread-events off
set environment PC_INPUT_SCRIPT vs
break main
run
set capture_attempted = 1
delete 1

# fn_8025A560 runs once per frame with the cursor gobj, by which point the
# icon jobjs have been placed and their matrices set up.
break mnStageSel_80259ED8
commands
silent
printf "PROBE: stage select geometry\n"
set $i = 0
while $i < 30
  printf "  slot %2d  stkind=0x%02x  x8=%d  jobj=%p", $i, mnStageSel_803F06D0[$i].xB, mnStageSel_803F06D0[$i].x8, mnStageSel_803F06D0[$i].x0
  if mnStageSel_803F06D0[$i].x0 != 0
    printf "  translate=%f %f  mtx=%f %f", mnStageSel_803F06D0[$i].x0->translate.x, mnStageSel_803F06D0[$i].x0->translate.y, mnStageSel_803F06D0[$i].x0->mtx[0][3], mnStageSel_803F06D0[$i].x0->mtx[1][3]
  end
  printf "  half=%f %f\n", mnStageSel_803F06D0[$i].xC, mnStageSel_803F06D0[$i].x10
  set $i = $i + 1
end
printf "  selected slot = %d\n", mnStageSel_804D6CAE
quit 0
end

break __assert
break abort
break pc_sys_exit
break OSPanic
break panicMissingStageParam

continue
printf "PROBE: stopped before stage select\n"
bt 8
quit 1
