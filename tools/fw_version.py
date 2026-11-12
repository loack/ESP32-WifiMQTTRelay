# Injecte le commit git (FW_GIT) dans la compilation ; "-modifie" si l'arbre suivi est modifie.
Import("env")
import subprocess
def sh(*a):
    try:
        return subprocess.check_output(a, cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""
rev = sh("git", "rev-parse", "--short=8", "HEAD") or "inconnu"
if sh("git", "status", "--porcelain", "--untracked-files=no"):
    rev += "-modifie"
env.Append(CPPDEFINES=[("FW_GIT", env.StringifyMacro(rev))])
