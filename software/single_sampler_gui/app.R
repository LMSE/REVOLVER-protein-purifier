## Load libraries
library(shiny)
library(shinythemes)
library(shinyFiles) # For accessing a directory for saving the protocol https://community.rstudio.com/t/shiny-directory-input/29160/2
library(serial)
library(beepr)

# Write command - snippet for reading buffer in serial package
#write.serialConnection(myArduino, "<SCAN,1>")
# Wait until the message is returned. 100 ms is enough
#Sys.sleep(0.1)
#msg <- read.serialConnection(myArduino)
# Get the message between the delimiters (see here)
#msg <- regmatches(msg, regexec('<(.*?)>', msg))[[1]][2]
# Split into characters separated by comma
#msg <- str_split(msg, pattern = ',')[[1]]

# more icons here https://fontawesome.com/v5.15/icons?d=gallery&p=2&q=file%20save

# Read instructions
# inst <- read.table("code.txt", sep = "\t", header = F)$V1
#
# for (i in (1:length(inst))){
#   # Write command
#   write.serialConnection(myArduino, inst[i])
#   # Wait for response - loop while the incoming buffer is empty or until it's not empty but it stops changing
#   # byt maybe adding a delay is simpler, it's just that we don't knw how long the instruction would take to execute
#   # and we want to keep the code busy
#   n_in = 0
#   while (n_in == 0 || (nBytesInQueue(myArduino)[1] - n_in > 0)){
#     n_in = nBytesInQueue(myArduino)[1]
#   }
#
#   print(nBytesInQueue(myArduino))
#   print(read.serialConnection(myArduino))
#
#
# }

## Initialize some global variables

# Initialize list of available ports - if no ports are connected, R will throw an error, so we use a tryCatch
portList <-
  tryCatch(
    expr = {
      listPorts()
    },
    error = function(e) {
      NULL
    }
  )
# Initialize connection status as disconnected
isConnected <- reactiveVal(value = FALSE)
# Initialize a serial connection object with an empty port for now
myArduino <-  serialConnection(
  port = "",
  mode = "9600,n,8,1" ,
  buffering = "none",
  newline = TRUE,
  eof = "",
  translation = "auto",
  # to chop the \n character
  handshake = "none",
  buffersize = 4096
)

## Define some useful functions 

# Function for sending serial commands - sends one at a time and waits for an "ok" message from the arduino before sending more
# Default null instructions, device is a serialConnection object. inst is a character vector, each element is an instruction
# with the appropriate syntax
sendCommandSerial <- function(inst = NULL, device){ 
  
  for (i in (1:length(inst))) {
    # Write command
    write.serialConnection(device, inst[i])
    # Wait for response - loop while the incoming buffer is empty or until it's not empty but it stops changing
    # NOTE: maybe adding a delay is simpler, it's just that we don't know how long the instruction would take to execute
    # and we want to keep the code busy
    n_in = 0
    while (n_in == 0 ||
           (nBytesInQueue(myArduino)[1] - n_in > 0)) {
      n_in = nBytesInQueue(myArduino)[1]
    }
    print(nBytesInQueue(myArduino))
    print(read.serialConnection(myArduino))
  }
}

## UI -----------------------------------------------------------
# Define UI
ui <- fluidPage(
  theme = shinytheme("slate"),
  
  # App title
  titlePanel(strong("REVOLVER: Automated protein purification system - Single device version")),
  hr(),
  # Panels in layout (columns per row must add to 12)
  
  ## Input panels: Left large column
  column(7,
         
         ## Row 1 - Connect arduino and manual serial input (panels 1 and 2) - nested columns
         fluidRow(
           ### Block 1: Select box for ports
           column(5,
                  wellPanel(
                    h3("1. Connect Arduino"),
                    fluidRow(
                      selectInput(inputId = "select_port", label = h4("Select port"), choices = portList)
                    ),
                    fluidRow(
                      # Button 1: Refresh list of serial ports
                      actionButton("button_refresh", "Update", icon = icon("refresh"), class = "btn-primary"),
                      # Button 2: Connect to port
                      actionButton("button_connect", "Connect", icon = icon("check"), class = "btn-primary"),
                      # Button 3: Disconnect
                      actionButton("button_disconnect", "Disconnect", icon = icon("times"), class = "btn-primary"),
                      # Text output 1: Status of connection
                      htmlOutput(outputId = "text_connection", style = "padding-top: 1em"),
                      align = "left"
                    ),
                    #style = "padding-left: 2em; height: 20em"
                  )
           ),
           
           ### Block 2: Manual serial input and control
           column(7,
                  wellPanel(
                    h3("2. Manual control"),
                    # Manual serial input
                    fluidRow(
                      column(8,
                             # Text input 1: For typing serial command for testing
                             textInput(inputId = "text_input", label = h4("Serial input"), value = ""),
                             style = "padding: 0px"),
                      column(4,
                             # Button 4: Send command
                             actionButton("button_serial", "Send", icon("share"), class = "btn-primary"),
                             style = "margin-top: 3em"
                      )
                    ),
                    # Manual (auto) home and rotation
                    fluidRow(
                      #style = "padding-left: 1em",
                      h4("Control buttons"),
                      # Extra buttons : Manual control
                      actionButton("button_home", "Auto", icon("home"), class = "btn-primary"),
                      actionButton("button_left_fast", "", icon("angle-double-left"), class = "btn-primary"),
                      actionButton("button_left", "", icon("angle-left"), class = "btn-primary"),
                      actionButton("button_right", "", icon("angle-right"), class = "btn-primary"),
                      actionButton("button_right_fast", "", icon("angle-double-right"), class = "btn-primary"),
                      actionButton("button_purge1", "Purge #1", icon("gas-pump"), class = "btn-primary"),
                      actionButton("button_purge2", "Purge #2", icon("gas-pump"), class = "btn-primary")
                    ),
                    #style = "padding: 1em; height: 20em"
                  )
           ),
         ),
         
         
         
         ## Row 2 - Protocol
         fluidRow(
           ### Block 3 - Protocol - full column
           column(12,
                  wellPanel(
                    h3("3. Protocol information"),
                    fluidRow(
                      # Selection of manual vs file protocol
                      radioButtons("radio_buttons", label = h4("Type of protocol to run"),
                                   choices = list("Manual" = 1, "File" = 2), 
                                   selected = 1, inline = T),
                      # Button 5: Run protocol
                      actionButton("button_run", "Run protocol", icon("share"), class = "btn-primary"),
                      # Button: Export protocol
                      actionButton("button_export", "Export", icon("file-export"), class = "btn-primary")
                    ),
                    # File input 1: Input file with protocol
                    fileInput(inputId = "file_input",
                              label = h4("Load protocol file")),
                    
                    
                    
                    # Input protocol manually
                    h4("Protocol assistant"),
                    fluidRow(
                      column(4,
                             # Number of washes
                             numericInput(
                               "num_washes",
                               label = h4("Number of washes"),
                               value = 1,
                               min = 1,
                               max = 5
                             ),
                             # Number of elutions
                             numericInput(
                               "num_washes",
                               label = h4("Number of elutions"),
                               value = 1,
                               min = 1,
                               max = 5
                             ),
                             
                             # Number of tubes
                             numericInput(
                               inputId = "slider_tubes",
                               label = h4("Tubes to collect"),
                               min = 1,
                               max = 12,
                               value = 10
                             ),
                             
                      ),
                      
                      column(4,
                             
                             # Volume per wash (mL)
                             textInput(
                               inputId = "volume_wash",
                               label = h4("Volume per wash (mL)"),
                               value = ""
                             ),
                             
                             # Volume per elution (mL)
                             textInput(
                               inputId = "volume_elution",
                               label = h4("Volume per elution (mL)"),
                               value = ""
                             ),
                             
                             # Name of protocol
                             textInput(
                               inputId = "name_protocol",
                               label = h4("File title for protocol"),
                               value = "name.txt"
                             ),
                             
                             
                             
                      ),
                      
                    )
                    
                  )
           ),
           
         ),
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
  ),# end of big left column
  
  ## Info panel - smaller left column
  column(5,
         h3("4. REVOLVER status"),
         br(),
         # Image of REVOLVER - using HTML command instead of img since it's easier to center that way
         HTML('<center><img src="revolver_img.png" height="400"></center>'),
         # Text output 2: Status of REVOLVER
         htmlOutput(outputId = "text_status", style = "padding-top: 1em")
         
         
  )
  
  
) # end UI

## Server ----------------------------------------------------------
# Define server logic
server <- function(input, output) {
  
  ## Events corresponding to block 1: Connect arduino
  
  # Observe refresh button for updating serial ports
  observeEvent(input$button_refresh, {
    updateSelectInput(inputId = "select_port", choices = tryCatch(
      expr = {
        listPorts()
      },
      error = function(e) {
        NULL
      }
    ))
  })
  # Observe port list to set a tentative port name
  observeEvent(input$select_port, {
    myArduino$port <- input$select_port
  })
  # Observe button for connecting - only act if there is a non-empty port
  observeEvent(input$button_connect, {
    if (listPorts() != "") {
      myArduino$port <- input$select_port
      # Close first and re-open
      close(myArduino)
      open(myArduino)
      isConnected(isOpen(myArduino))
    }
    else {
      # Attempted to connect to a non-existent port - throw warning and reset port choices
      output$text_connection <-
        renderText("<b><span style=\"color:red; font-size:120%\">EMPTY PORT</span><b>")
      updateSelectInput(inputId = "select_port", choices = listPorts())
    }
  })
  
  # Observe button for disconnecting
  observeEvent(input$button_disconnect, {
    close(myArduino)
    isConnected(isOpen(myArduino))
  })
  
  ## Events corresponding to block 2: Sending serial command
  
  # Observe button for sending inline serial command
  observeEvent(input$button_serial, {
    msgOut <- input$text_input
    # Break single line into multiple commands delimited by <> just so we can show a progress bar
    msgOut <- strsplit(msgOut, ">")[[1]] # split based on >
    msgOut <- paste0(msgOut, ">") # Add > back to each instruction
    # Send command and clear text box
    if (isOpen(myArduino)) {
      sendCommandSerial(msgOut, myArduino)
      updateTextInput(inputId = "text_input", value = "")
    }
  })
  
  # Observe connection status
  observeEvent(isConnected(), {
    if (isConnected() == TRUE) {
      output$text_connection <-
        renderText("<b><span style=\"color:green; font-size:120%\">CONNECTED</span><b>")
    }
    else{
      output$text_connection <-
        renderText("<b><span style=\"color:red; font-size:120%\">DISCONNECTED</span><b>")
    }
  })
  
  # Observe button for running protocol
  observeEvent(input$button_run, {
    # Send command if connection is open
    if (isOpen(myArduino)) {
      # Read and parse file to make it a character vector
      inst_file <- input$file_input
      msgOut <- read.table(inst_file$datapath, sep = "\t", header = F)$V1
      # Execute the instructions
      sendCommandSerial(msgOut, myArduino)
    }
  })
  
  # Observe event for exporting protocol file
  observeEvent(input$button_export, {
    # Choose directory - using a function from rstudioapi package that is platform-independent, but assumes user is in RStudio
    dir_export <- rstudioapi::selectDirectory(caption = "Select folder for exporting protocol")
    # Initialize variable for protocol
    prot <- NULL
    # Parse the inputs for the protocol - assuming auto home always happens
    prot[1] <- "<H>"
    # TO DO : What is the order of the operations??
    # Export each element as a line in the text file
    writeLines(prot, con = paste0(dir_export, "/", input$name_protocol))
  })
  
  # Observe buttons for manual rotation, homing, and purging
  observeEvent(input$button_home, {
    if (isOpen(myArduino)) {
      write.serialConnection(myArduino, "<H>")
    }
  })
  observeEvent(input$button_left_fast, {
    # Send command and clear text box
    if (isOpen(myArduino)) {
      write.serialConnection(myArduino, "<R,100,0>")
    }
  })
  observeEvent(input$button_left, {
    # Send command and clear text box
    if (isOpen(myArduino)) {
      write.serialConnection(myArduino, "<R,10,0>")
    }
  })
  observeEvent(input$button_right_fast, {
    # Send command and clear text box
    if (isOpen(myArduino)) {
      write.serialConnection(myArduino, "<R,100,1>")
    }
  })
  observeEvent(input$button_right, {
    # Send command and clear text box
    if (isOpen(myArduino)) {
      write.serialConnection(myArduino, "<R,10,1>")
    }
  })
  
  
  
  
} # end of server

# --------------------------------------------------------------
shinyApp(ui = ui, server = server)