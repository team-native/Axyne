#!/bin/zsh

set -u

script_directory="${0:A:h}"
if [[ -d "${script_directory}/Axyne.app" ]]; then
    app_path="${script_directory}/Axyne.app"
elif [[ -d "/Applications/Axyne.app" ]]; then
    app_path="/Applications/Axyne.app"
else
    osascript -e 'display alert "Axyne Uninstaller" message "Axyne.app was not found next to this uninstaller or in /Applications." as warning buttons {"OK"}'
    exit 1
fi

osascript - "${app_path}" <<'APPLESCRIPT'
on run argv
    set appPath to item 1 of argv
    set confirmation to display dialog "Remove Axyne from this Mac?" & return & return & appPath buttons {"Cancel", "Uninstall"} default button "Cancel" with icon caution
    if button returned of confirmation is not "Uninstall" then
        return
    end if

    try
        do shell script "/bin/rm -rf -- " & quoted form of appPath with administrator privileges
        display dialog "Axyne was removed." buttons {"OK"} with icon note
    on error errorMessage
        display alert "Axyne could not be removed" message errorMessage as warning buttons {"OK"}
        error number -128
    end try
end run
APPLESCRIPT
