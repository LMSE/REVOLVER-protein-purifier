## Load libraries
library(shiny)
library(shinythemes)
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


## Define UI 
ui <- fluidPage(theme = shinytheme("darkly"),
  
  # App title ----
  titlePanel(strong("REVOLVER: Automated protein purification system")),
  
  # Panels in layout (columns per row must add to 12)
  fluidRow(
    # Select box for ports
    selectInput(inputId = "select_port",
                label = h4("Connect Arduino"),
                choices = portList),
    # Button 1: Refresh list of serial ports
    actionButton("button_refresh", "Get ports"),
    # Button 2: Connect to port
    actionButton("button_connect", "Connect"),
    # Button 3: Disconnect
    actionButton("button_disconnect", "Disconnect"),
    # Text output 1: Status of connection
    htmlOutput(outputId = "text_connection"),
    
    # Text input 1: For typing serial command for testing
    textInput(inputId = "text_input", 
              label = h4("Serial input"), 
              value = ""),
    # Button 4: Send command
    actionButton("button_serial", "Send"),
    
    # Slider 1: Choose number of tubes
    sliderInput(inputId = "slider_tubes", 
                label = h4("Tubes to collect"), 
                min = 1, max = 12, value = 10)
  )

  
) # end UI

# ----------------------------------------------------------
## Define server logic
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
    # Read command
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
  

  
} # end of server

# --------------------------------------------------------------
shinyApp(ui = ui, server = server)