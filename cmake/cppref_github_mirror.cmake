# Rewrite https://github.com/owner/repo.git -> ${CPPREF_GITHUB_MIRROR_PREFIX}/owner/repo.git

set(CPPREF_GITHUB_MIRROR_PREFIX "https://gh.bugdey.us.kg/https://github.com"
    CACHE STRING "GitHub mirror prefix (no trailing slash)")

function(cppref_github_mirror url out_var)
  string(REGEX REPLACE "^https://github\\.com" "${CPPREF_GITHUB_MIRROR_PREFIX}" _m "${url}")
  set(${out_var} "${_m}" PARENT_SCOPE)
endfunction()
