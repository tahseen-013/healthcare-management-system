# Healthcare Management System (console, C)

    make          # build
    make run      # build and run
    make clean    # remove the program (keeps patient data)
    make distclean  # also deletes patients.tsv

Login: username `admin`. Default password `admin123`; set the
`HEALTHCARE_ADMIN_PASSWORD` environment variable to use your own.

Patient data is saved to `patients.tsv` (tab-separated text) after every change.
