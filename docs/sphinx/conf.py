"""Render Markdown guides and Doxygen XML in one Sphinx documentation site."""

import os

project = "Socket Chat"
author = "Socket Chat team"
extensions = ["breathe", "myst_parser", "sphinxcontrib.mermaid"]
source_suffix = {".rst": "restructuredtext", ".md": "markdown"}
root_doc = "docs/sphinx/index"
exclude_patterns = []
html_theme = "alabaster"
html_title = "Socket Chat documentation"
html_static_path = []
myst_enable_extensions = ["colon_fence"]
myst_fence_as_directive = ["mermaid"]
myst_heading_anchors = 4
# Doxygen is run before Sphinx, by the build driver.
breathe_projects = {"socket_chat": os.environ["CHAT_DOXYGEN_XML"]}
breathe_default_project = "socket_chat"
breathe_default_members = ("members",)
breathe_domain_by_extension = {"hpp": "cpp", "cpp": "cpp"}

