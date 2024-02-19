# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "SecretFlow PSI Library"
copyright = "2023, SecretFlow authors"
author = "SecretFlow authors"

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "myst_parser",
    "sphinx.ext.extlinks",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]


# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "alabaster"
html_static_path = ["_static"]

# -- Options for  sphinx-int -------------------------------------------------
locale_dirs = ["locale/"]  # path is example but recommended.
gettext_compact = False  # optional.


# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

# global variables
extlinks = {
    "psi_doc_host": ("https://www.secretflow.org.cn/docs/psi/en/", "doc "),
    "psi_code_host": ("https://github.com/secretflow/psi/", "code "),
}

html_theme = "pydata_sphinx_theme"
html_static_path = ["_static"]

# Enable TODO
todo_include_todos = True

autodoc_default_options = {
    "members": True,
    "member-order": "bysource",
    "special-members": "__init__",
    "undoc-members": False,
    "show-inheritance": False,
}


html_favicon = "_static/favicon.ico"

html_css_files = [
    "css/custom.css",
]

html_js_files = ["js/custom.js"]

html_theme_options = {
    "icon_links": [
        {
            "name": "GitHub",
            "url": "https://github.com/secretflow/psi",
            "icon": "fab fa-github-square",
            "type": "fontawesome",
        },
    ],
    "logo": {
        "text": "Secretflow PSI Library",
    },
    "show_nav_level": 4,
    "language_switch_button": True,
}
