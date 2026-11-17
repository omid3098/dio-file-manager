# WRITTEN BY tools/update_cui.py. Do not edit by hand.
# This pins CUI to omid3098's fork, branch dfm-rtl-input: upstream plus yappologistic/CUI pull request #12, the right-to-left
# text box editing. A hand edit that points this at upstream drops the Persian text box fix: Persian typed into a text box
# draws unjoined and left to right, and CMakeLists.txt stops the configure.
# To move CUI to a newer upstream commit, run:   python tools/update_cui.py <upstream-commit>
set(DFM_CUI_REPOSITORY "https://github.com/omid3098/CUI-yappologistic.git")
set(DFM_CUI_COMMIT "57ccb47d4373e4c36788162b0b43480b88296c55")
