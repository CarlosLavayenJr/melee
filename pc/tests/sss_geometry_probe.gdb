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

# The icons animate in, so an early sample catches them before their matrices
# have been placed -- the first attempt at this probe broke on
# mnStageSel_80259ED8 and got positions that were clustered within a fifth of
# a unit of each other and duplicated across ten slots, which is what a
# half-built model looks like rather than a six-column grid. This counts
# frames of the scene and dumps once the screen has settled.
set $frames = 0
break mnStageSel_80259C28
commands
silent
set $frames = $frames + 1
if $frames < 90
  # Hold the screen open for the measurement. The route presses Start on its
  # first stage-select frame, which would confirm a stage and tear the model
  # down long before the icons have finished animating in. Zeroing the button
  # word this function tests makes the press a no-op for these frames. This is
  # a measuring intervention in a probe, not behaviour the port ships.
  set mnStageSel_804D6CA0 = 0
  continue
end
printf "PROBE: stage select geometry at frame %d\n", $frames
set $i = 0
while $i < 30
  printf "  slot %2d  stkind=0x%02x  x8=%d", $i, mnStageSel_803F06D0[$i].xB, mnStageSel_803F06D0[$i].x8
  if mnStageSel_803F06D0[$i].x0 != 0
    # mtx is only valid after HSD_JObjSetupMatrix, which is why lb_8000B1CC
    # -- the function the game itself hit-tests with -- calls it first. Read
    # without it and every icon reports the same stale root transform, which
    # is what the first two runs of this probe printed.
    call (void) HSD_JObjSetupMatrix(mnStageSel_803F06D0[$i].x0)
    printf "  world=%f %f", mnStageSel_803F06D0[$i].x0->mtx[0][3], mnStageSel_803F06D0[$i].x0->mtx[1][3]
  end
  printf "  half=%f %f\n", mnStageSel_803F06D0[$i].xC, mnStageSel_803F06D0[$i].x10
  set $i = $i + 1
end
printf "  selected slot = %d\n", mnStageSel_804D6CAE
printf "  cursor gobj = %p\n", mnStageSel_804D6C9C
if mnStageSel_804D6C9C != 0
  printf "  cursor local translate = %f %f\n", ((HSD_JObj*) mnStageSel_804D6C9C->hsd_obj)->translate.x, ((HSD_JObj*) mnStageSel_804D6C9C->hsd_obj)->translate.y
  printf "  cursor world           = %f %f\n", ((HSD_JObj*) mnStageSel_804D6C9C->hsd_obj)->mtx[0][3], ((HSD_JObj*) mnStageSel_804D6C9C->hsd_obj)->mtx[1][3]
end
quit 0
end

break __assert
break abort
break pc_sys_exit
break OSPanic
break panicMissingStageParam

continue
printf "PROBE: stopped before stage select settled\n"
bt 8
quit 1
