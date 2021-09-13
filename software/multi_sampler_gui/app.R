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

# Initialize list of available ports
portList <- listPorts()
# Initialize connection status as disconnected
isConnected <- reactiveVal(value = FALSE)
# Initialize a serial connection object with an empty port for now
myArduino <-  serialConnection(
  port = "",
  mode = "9600,n,8,1" ,
  buffering = "none",
  newline = TRUE,
  eof = "",
  translation = "auto", # to chop the \n character
  handshake = "none",
  buffersize = 4096
)

## UI -----------------------------------------------------------
# Define UI 
ui <- fluidPage(theme = shinytheme("darkly"),
  
  # App title 
  titlePanel(strong("REVOLVER: Automated protein purification system")),
  h3(" - Single device version - "),
  # Panels in layout (columns per row must add to 12)
  fluidRow(
    
    ### Block 1: Select box for ports
    
    wellPanel(h3("1. Connect Arduino"),
           
    selectInput(inputId = "select_port", label = h5("Port"), choices = portList),
    # Button 1: Refresh list of serial ports
    actionButton("button_refresh", "Update", icon = icon("refresh")),
    # Button 2: Connect to port
    actionButton("button_connect", "Connect", icon = icon("check")),
    # Button 3: Disconnect
    actionButton("button_disconnect", "Disconnect", icon = icon("times")),
    # Text output 1: Status of connection
    htmlOutput(outputId = "text_connection")),
    
    ### Block 2: Manual serial input
    
    # Text input 1: For typing serial command for testing
    textInput(inputId = "text_input", 
              label = h4("Serial input"), 
              value = ""),
    # Button 4: Send command
    actionButton("button_serial", "Send", icon("share")),
    
    
    # File input 1: Input file with protocol
    fileInput(inputId = "file_input", 
              label = h4("Protocol file")),
    
    # Button 5: Run protocol
    actionButton("button_run", "Run protocol", icon("share")),
    
    
    
    ### Block 3: Manual rotation
    actionButton("button_home", "Auto", icon("home")),
    actionButton("button_left_fast", "", icon("angle-double-left")),
    actionButton("button_left", "", icon("angle-left")),
    actionButton("button_right", "", icon("angle-right")),
    actionButton("button_right_fast", "", icon("angle-double-right")),
    actionButton("button_purge1", "Purge #1", icon("gas-pump")),
    actionButton("button_purge2", "Purge #2", icon("gas-pump")),
    
    
    img(src = "revolver.png", height = 100, width = 100),
    
    

    
  ),
  
  # Input protocol
  fluidRow(
    column(width = 4,
    # Number of washes
    numericInput("num_washes", 
                 label = h4("Number of washes"),
                 value = 1, min = 1, max = 5),
    # Number of elutions
    numericInput("num_washes", 
                 label = h4("Number of elutions"),
                 value = 1, min = 1, max = 5),
    
    # Number of tubes
    numericInput(inputId = "slider_tubes", 
                 label = h4("Tubes to collect"), 
                 min = 1, max = 12, value = 10),
    
    ),
    
    column(width = 4,
    
    # Volume per wash (mL)
    textInput(inputId = "volume_wash", 
              label = h4("Volume per wash (mL)"), 
              value = ""),
    
    # Volume per elution (mL)
    textInput(inputId = "volume_elution", 
              label = h4("Volume per elution (mL)"), 
              value = ""),
    
    
    
    ),
    
    actionButton("button_export", "Export", icon("file-export"))
    
  )

  
) # end UI

## Server ----------------------------------------------------------
# Define server logic
server <- function(input, output) {
  
  
  # Observe refresh button for updating serial ports
  observeEvent(input$button_refresh,{
    updateSelectInput(inputId = "select_port", choices = listPorts())
  })
  # Observe port list to set a tentative port name
  observeEvent(input$select_port,{
    myArduino$port <- input$select_port
  })
  # Observe button for connecting - only act if there is a non-empty port 
  observeEvent(input$button_connect,{
    if (listPorts() != ""){
      myArduino$port <- input$select_port
      # Close first and re-open
      close(myArduino)
      open(myArduino)
      isConnected(isOpen(myArduino))}
    else {
      # Attempted to connect to a non-existent port - throw warning and reset port choices
      output$text_connection <- renderText(paste("<b><span style=\"color:red\">EMPTY PORT</span></b>"))
      updateSelectInput(inputId = "select_port", choices = listPorts())
      }
  })
  
  # Observe button for disconnecting 
  observeEvent(input$button_disconnect,{
    close(myArduino)
    isConnected(isOpen(myArduino))
  })
  
  # Observe button for sending serial command 
  observeEvent(input$button_serial,{
    msgOut <- input$text_input
    # Send command and clear text box
    if (isOpen(myArduino)){
      write.serialConnection(myArduino, msgOut)
      updateTextInput(inputId = "text_input", value = "")
     }
  })
  
  # Observe connection status
  observeEvent(isConnected(),{
    if (isConnected() == TRUE){
      output$text_connection <- renderText(paste("<b><span style=\"color:green\">CONNECTED</span></b>"))
    }
    else{
      output$text_connection <- renderText(paste("<b><span style=\"color:red\">DISCONNECTED</span></b>"))
    }
  })
  
  # Observe button for running protocol
  observeEvent(input$button_run,{
    # Send command and clear text box
    if (isOpen(myArduino)){
      inst_file <- input$file_input
      inst <- read.table(inst_file$datapath, sep = "\t", header = F)$V1
      
      for (i in (1:length(inst))){
        # Write command
        write.serialConnection(myArduino, inst[i])
        # Wait for response - loop while the incoming buffer is empty or until it's not empty but it stops changing
        # byt maybe adding a delay is simpler, it's just that we don't knw how long the instruction would take to execute
        # and we want to keep the code busy
        n_in = 0
        while (n_in == 0 || (nBytesInQueue(myArduino)[1] - n_in > 0)){
          n_in = nBytesInQueue(myArduino)[1]
        }
        print(nBytesInQueue(myArduino))
        print(read.serialConnection(myArduino))
      }
    }
  })
  
  # Observe buttons for manual rotation
  observeEvent(input$button_home,{
    if (isOpen(myArduino)){write.serialConnection(myArduino, "<H>")}
  })
  observeEvent(input$button_left_fast,{
    # Send command and clear text box
    if (isOpen(myArduino)){write.serialConnection(myArduino, "<R,100,0>")}
  })
  observeEvent(input$button_left,{
    # Send command and clear text box
    if (isOpen(myArduino)){write.serialConnection(myArduino, "<R,10,0>")}
  })
  observeEvent(input$button_right_fast,{
    # Send command and clear text box
    if (isOpen(myArduino)){write.serialConnection(myArduino, "<R,100,1>")}
  })
  observeEvent(input$button_right,{
    # Send command and clear text box
    if (isOpen(myArduino)){write.serialConnection(myArduino, "<R,10,1>")}
  })
  
  

  
} # end of server

# --------------------------------------------------------------
shinyApp(ui = ui, server = server)