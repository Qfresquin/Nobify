The files in this directory are the frozen legacy build-model implementation.

They are intentionally decoupled from the active architecture because the new
Event IR is now the primary semantic contract, and the old build model was
designed around the obsolete structural Event IR.

Future build-model work should derive from the new canonical Event IR instead
of extending these files.
