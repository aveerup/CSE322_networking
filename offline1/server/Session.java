package server;
import java.io.*;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.*;
import java.time.LocalDateTime;

public class Session extends Thread {
    private Socket socket;
    private boolean stopSession = false;
    private boolean authenticated = false;
    private BufferedWriter userListWriter = null;
    private Map<String, Boolean> users = null;
    private Map<String, User> userMessageWriter = null;
    private Map<String, String> files = null;
    private String user = null;
    private BufferedReader logReader = null;
    private BufferedWriter logWriter = null;
    private BufferedReader userDataReader = null;
    private BufferedWriter userDataWriter = null;
    private BufferedReader userPublicFileReader = null;
    private BufferedWriter userPublicFileWriter = null;
    private BufferedReader userPrivateFileReader = null;
    private BufferedWriter userPrivateFileWriter = null;
    private ObjectInputStream in = null;
    private ObjectOutputStream out = null;
    private ServerConfig serverConfig = null;
    private Integer MSG_NO = 0;
    private Integer FILE_NO = 0;
    private Long UPLOAD_SIZE = 0L;
    private Long DOWNLOAD_SIZE = 0L;
    private static final Object msgNoLock = new Object();
    private static final Object fileNoLock = new Object();
    private static final Object uploadLock = new Object();
    private static final Object downloadLock = new Object();
    private static final Object userDataLock = new Object();
    private static final Object logLock = new Object();
    private static final Object privateFileLock = new Object();
    private static final Object publicFileLock = new Object();
    private String line = null;

    public Session(Socket socket, 
                BufferedWriter userListWriter, 
                Map<String, Boolean> users,
                Map<String, String> files,
                ServerConfig serverConfig,
                Map<String, User> userMessageWriter) {

        this.socket = socket;
        this.userListWriter = userListWriter;
        this.users = users;
        this.serverConfig = serverConfig;
        this.userMessageWriter = userMessageWriter;

        // System.out.println("session " + MAX_CHUNK_SIZE + " " + MIN_CHUNK_SIZE);
        // System.out.println("session " + this.MAX_CHUNK_SIZE + " " + this.MIN_CHUNK_SIZE);

    }

    private boolean Download(String user, String fileName) {
        
        try {
            BufferedInputStream readFile = new BufferedInputStream(new FileInputStream("./userData/" + user + "/" + fileName));
            
            socket.setSoTimeout(7000);

            Integer downloadSize = 0;

            while(true) {

                byte[] chunk = new byte[serverConfig.MAX_CHUNK_SIZE];

                Integer readBytes = readFile.read(chunk);
                out.writeObject(readBytes);

                if(readBytes == -1) {
                    socket.setSoTimeout(0);
                    out.writeObject("file download complete");

                    String fileID = "";
                    synchronized(fileNoLock) {
                        FILE_NO += 1;
                        fileID += MSG_NO;
                        fileID += user;
                    }

                    synchronized(downloadLock) {
                        DOWNLOAD_SIZE += downloadSize;

                    }

                    synchronized(userDataLock) {

                        try(BufferedWriter userDataWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/userData.txt"))) {
                            userDataWriter.write(String.format("""
                                        upload_size, %d
                                        download_size, %d
                                        file_no, %d
                                        msg_no, %d
                                        """, UPLOAD_SIZE, DOWNLOAD_SIZE, FILE_NO, MSG_NO));
                        
                        
                            userDataWriter.newLine();                
                            userDataWriter.flush();

                        } catch(FileNotFoundException e) {
                            System.out.println("\ndownload " + e);
                        }
                    }
                        
                        
                    
                    
                    synchronized(logLock) {
                        logWriter.write(">>>>> download || ID " + fileID + " || name " + fileName + " || time " + LocalDateTime.now());
                        logWriter.newLine();
                        logWriter.flush();
                    }
                    
                    readFile.close();
                    
                    return true;
                }

                out.writeObject(chunk);
                
                downloadSize += readBytes;

                String response = (String) in.readObject();

                if(response != null && response.equalsIgnoreCase("received"))
                    continue;
            }
            
        } catch (SocketTimeoutException e) {

            try {
                out.writeObject("file download failed");
                socket.setSoTimeout(0);
            } catch (IOException io) {
                System.out.println("socket time out and io exception happended");
            }

            return false;

        } catch (IOException | ClassNotFoundException e) {
        
            System.out.println(e);
            return false;
        
        }
    }

    @Override
    public void run() {
        try {
            in = new ObjectInputStream(socket.getInputStream());
            out = new ObjectOutputStream(socket.getOutputStream());

            System.out.println("Handling client at " + socket.getInetAddress().getHostAddress());

            while(!authenticated) {
                user = (String) in.readObject();

                if(users.containsKey(user)) {
                    if(users.get(user)) {
                        out.writeObject("\nUser with same name exists, try another username");
                    } else {
                        authenticated = true;
                    }
                }else {

                    synchronized(serverConfig.serverConfigLock) {
                        try {
                            BufferedWriter serverConfigWriter = new BufferedWriter(new FileWriter("./server/serverConfig.txt"));
                            
                            if(serverConfig.CURR_USER_NUM + 1 <= serverConfig.MAX_USER_NUM) {
                                serverConfig.CURR_USER_NUM += 1;
                                serverConfigWriter.write(String.format("""
                                        max_buffer_size, %d
                                        min_chunk_size, %d
                                        max_chunk_size, %d
                                        curr_buffer_size, %d
                                        max_user_num, %d
                                        curr_user_num, %d
                                        """,serverConfig.MAX_BUFFER_SIZE,
                                            serverConfig.MIN_CHUNK_SIZE,
                                            serverConfig.MAX_CHUNK_SIZE,
                                            serverConfig.CURR_BUFFER_SIZE,
                                            serverConfig.MAX_USER_NUM,
                                            serverConfig.CURR_USER_NUM));
                            } else {
                                out.writeObject("\n!!!!!  Maximum user limit of server reached, log in as an existing user  !!!! ");
                                continue;
                            }

                            serverConfigWriter.close();
                        } catch (IOException e) {
                            System.out.println("server config write " + e);
                        }
                    }

                    synchronized(userListWriter) {
                        userListWriter.write(user + "\n");
                        userListWriter.flush();
                    }


                    File userDir = new File("./userData/" + user);
                    userDir.mkdir();
                    
                    File userHistory = new File("./userData/" + user + "/log.txt");
                    userHistory.createNewFile();

                    File userData = new File("./userData/" + user + "/userData.txt");
                    userData.createNewFile();

                    File userPublicFile = new File("./userData/" + user + "/userPublicFile.txt");
                    userPublicFile.createNewFile();

                    File userPrivateFile = new File("./userData/" + user + "/userPrivateFile.txt");
                    userPrivateFile.createNewFile();

                    File personalMessages = new File("./userData/" + user + "/personalMessages.txt");
                    personalMessages.createNewFile();
                    
                    File unreadPersonalMessages = new File("./userData/" + user + "/unreadPersonalMessages.txt");
                    unreadPersonalMessages.createNewFile();

                    userMessageWriter.put(user, new User(user));
                    
                    authenticated = true;
                }
            }

            users.put(user, true);
            out.writeObject("success");

            System.out.println("\nuser ===== " + user + " ===== logged into the server");

            logWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/log.txt", true));
            
            userDataReader = new BufferedReader(new FileReader("./userData/" + user + "/userData.txt"));

            while((line = userDataReader.readLine()) != null) {
                String[] lines = line.split(",");
                switch(lines[0]) {
                    case "upload_size":
                        UPLOAD_SIZE = Long.parseLong(lines[1].trim());
                        break;
                    case "download_size":
                        DOWNLOAD_SIZE = Long.parseLong(lines[1].trim());
                        break;
                    case "file_no":
                        FILE_NO = Integer.parseInt(lines[1].trim());
                        break;
                    case "msg_no":
                        MSG_NO = Integer.parseInt(lines[1].trim());
                        break;                        
                }
            }
            userDataReader.close();

            userPublicFileWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/userPublicFile.txt", true));
            
            userPrivateFileReader = new BufferedReader(new FileReader("./userData/" + user + "/userPrivateFile.txt"));
            userPrivateFileWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/userPrivateFile.txt", true));

            while(!stopSession) {
                String operation = (String) in.readObject();

                switch(operation) {
                    case "1":
                        String user_activityStatus = "\n";

                        for(String userName:users.keySet()) {
                            if(users.get(userName)) {
                                user_activityStatus += (userName + " online \n");
                            }else {
                                user_activityStatus += (userName + " offline \n");
                            }
                        }
                        
                        out.writeObject(user_activityStatus);
                        
                        break;
                    
                    case "2":
                        userPublicFileReader = new BufferedReader(new FileReader("./userData/" + user + "/userPublicFile.txt"));

                        String files = "";

                        files += "public\n";
                        files += "=========\n";

                        while((line = userPublicFileReader.readLine()) != null) {
                            files += line;
                            files += "\n";
                        } 

                        files += "private\n";
                        files += "=========\n";

                        while((line = userPrivateFileReader.readLine()) != null) {
                            files += line;
                            files += "\n";
                        }

                        userPublicFileReader.close();

                        out.writeObject(files);

                        String response = (String) in.readObject();
                        if(response.equalsIgnoreCase("download")) {
                            String fileName = (String) in.readObject();
                            Download(user, fileName);
                        } else if(response.equalsIgnoreCase("cancel")) {
                            continue;
                        }

                        break;

                    case "3":
                        String userToLookUp = (String) in.readObject();
                        if(!users.containsKey(userToLookUp)) {
                        
                            out.writeObject("No user found");
                        
                        } else {
                        
                            out.writeObject("user found");

                            String otherUserFiles = "";

                            BufferedReader userPublicFileReader = new BufferedReader(new FileReader("./userData/" + userToLookUp + "/userPublicFile.txt"));

                            String line = null;
                            while((line = userPublicFileReader.readLine()) != null) {
                                otherUserFiles += line;
                                otherUserFiles += "\n";
                            }

                            out.writeObject(otherUserFiles);

                            userPublicFileReader.close();

                            response = (String) in.readObject();
                            if(response.equalsIgnoreCase("download")) {
                                String fileName = (String) in.readObject();
                                Download(userToLookUp, fileName);
                            } else if(response.equalsIgnoreCase("cancel")) {
                                continue;
                            }
                        }

                        break;

                    case "4":
                        String description = (String) in.readObject();

                        String option = (String) in.readObject();
                        if(option.equalsIgnoreCase("1")) {

                            String recipient = (String) in.readObject();
                            System.out.println("recipient " + recipient);
                            if(!user.equalsIgnoreCase(recipient) && users.containsKey(recipient)) {

                                BufferedWriter userUnreadPersonalMessageWriter = userMessageWriter.get(recipient).unreadPersonalMessagesWriter;
                                System.out.println("1.9");

                                String msgID = "";
                                synchronized(msgNoLock) {
                                    MSG_NO += 1;
                                    msgID += MSG_NO;
                                    msgID += user;
                                }

                                synchronized(userUnreadPersonalMessageWriter) {
                                    userUnreadPersonalMessageWriter.write(msgID + " | " + description + " | " + user + " | " + "personal" + " | " + LocalDateTime.now());
                                    userUnreadPersonalMessageWriter.newLine();
                                    userUnreadPersonalMessageWriter.flush();
                                }
                                System.out.println("2.0");
                                out.writeObject("message sent successfully");
                            
                            } else {
                                out.writeObject("recipient not valid");

                            }

                        } else if(option.equalsIgnoreCase("2")) {

                            String msgID = "";
                            synchronized(msgNoLock) {
                                MSG_NO += 1;
                                msgID += MSG_NO;
                                msgID += user;
                            }
                            
                            for(String userName : users.keySet()) {
                                if(userName.equalsIgnoreCase(user))
                                    continue;

                                BufferedWriter userUnreadPersonalMessageWriter = userMessageWriter.get(userName).unreadPersonalMessagesWriter;

                                synchronized(userUnreadPersonalMessageWriter) {
                                    userUnreadPersonalMessageWriter.write(msgID + " | " + description + " | " + user + " | " + "general" + " | " + LocalDateTime.now());
                                    userUnreadPersonalMessageWriter.newLine();
                                    userUnreadPersonalMessageWriter.flush();
                                }
                            }

                            out.writeObject("message sent successfully");

                        } else if(option.equalsIgnoreCase("3")) {
                            continue;
                        }

                        break;
                    
                    case "5":
                        BufferedReader userUnreadPersonalMessageReader = new BufferedReader(new FileReader("./userData/" + user + "/unreadPersonalMessages.txt"));
                        BufferedWriter userPersonalMessageWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/personalMessages.txt", true));
                        
                        String unreadMessages = "";
                        while((line = userUnreadPersonalMessageReader.readLine()) != null) {
                            unreadMessages += line;
                            unreadMessages += "\n";
                        }

                        if(unreadMessages.equalsIgnoreCase("")) {
                            unreadMessages = "!!!  No unseen messages left  !!!";
                        }

                        out.writeObject(unreadMessages);
                        
                        userPersonalMessageWriter.write(unreadMessages);
                        userPersonalMessageWriter.flush();
                        
                        // clear the unreadPersonalMessages file
                        new FileWriter("./userData/" + user + "/unreadPersonalMessages.txt", false).close();
                        
                        userUnreadPersonalMessageReader.close();
                        userPersonalMessageWriter.close();

                        break;

                    case "6":
                        String[] fileNameSize = ((String)in.readObject()).split(",");
                        System.out.println(fileNameSize[0] + " " + fileNameSize[1]);
                        
                        if(Long.parseLong(fileNameSize[1].trim()) + serverConfig.CURR_BUFFER_SIZE < serverConfig.MAX_BUFFER_SIZE) {
                            
                            System.out.println("success");
                            out.writeObject("success");

                            // create unique file id
                            String fileID = null;
                            synchronized(fileNoLock) {
                                FILE_NO += 1;
                                fileID = Integer.toString(FILE_NO) + user;
                            }
                            
                            // choose random chunk size 
                            Random r = new Random(); 
                            int num = r.nextInt(serverConfig.MAX_CHUNK_SIZE - serverConfig.MIN_CHUNK_SIZE + 1) + serverConfig.MIN_CHUNK_SIZE;
                            System.out.println("num " + num);
                            
                            out.writeObject(fileID + ", " + Integer.toString(num));

                            File file = new File("./userData/" + user + "/" + fileNameSize[0]);
                            file.createNewFile();

                            BufferedOutputStream fileWrite = new BufferedOutputStream(new FileOutputStream(file));
                            Long uploaded = 0L;

                            socket.setSoTimeout(7000);

                            try {
                                while(true) {
                                    Integer readBytes = (Integer) in.readObject();
                                    System.out.println("readBytes " + readBytes);
                                    
                                    if(readBytes == -1)
                                        break;

                                    byte[] chunk = (byte[]) in.readObject();
                                    System.out.println("chunk " + chunk);
                                    
                                    uploaded += readBytes;
                                    fileWrite.write(chunk, 0, readBytes);

                                    out.writeObject("chunk upload success");

                                }


                                String status = (String) in.readObject();
                                System.out.println(status);
                                System.out.println(uploaded + " " + fileNameSize[1]);

                                socket.setSoTimeout(0);

                                if(status.equalsIgnoreCase("file upload complete") 
                                    && uploaded == Long.parseLong(fileNameSize[1].trim())) {
                                    
                                    out.writeObject("file upload success");

                                    line = (String) in.readObject();
                                    if(line.equalsIgnoreCase("private")) {
                                        
                                        synchronized(privateFileLock) {
                                            userPrivateFileWriter.write(fileID + " -- " + fileNameSize[0] + " -- " + fileNameSize[1]);
                                            userPrivateFileWriter.newLine();
                                            userPrivateFileWriter.flush();
                                        }

                                    } else if(line.equalsIgnoreCase("public")) {

                                        synchronized(publicFileLock) {
                                            userPublicFileWriter.write(fileID + " -- " + fileNameSize[0] + " -- " + fileNameSize[1]);
                                            userPublicFileWriter.newLine();
                                            userPublicFileWriter.flush();
                                        }

                                    }

                                    synchronized(uploadLock) {
                                        UPLOAD_SIZE += Long.parseLong(fileNameSize[1].trim());

                                    }

                                    synchronized(userDataLock) {
                                        
                                        try(BufferedWriter userDataWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/userData.txt"))) {
                                            userDataWriter.write(String.format("""
                                                        upload_size, %d
                                                        download_size, %d
                                                        file_no, %d
                                                        msg_no, %d
                                                        """, UPLOAD_SIZE, DOWNLOAD_SIZE, FILE_NO, MSG_NO));
                                        
                                        
                                            userDataWriter.newLine();                
                                            userDataWriter.flush();

                                        } catch(FileNotFoundException e) {
                                            System.out.println("\nupload " + e);
                                        }
                                        
                                    }

                                    synchronized(logLock) {
                                        logWriter.write(">>>>> upload || ID " + fileID + " || name " + fileNameSize[0] + " || time " + LocalDateTime.now());
                                        logWriter.newLine();
                                        logWriter.flush();
                                    }
                                    
                                }

                                fileWrite.close();
                            
                            } catch(SocketTimeoutException e) {
                                out.writeObject("file upload failed");
                                socket.setSoTimeout(0);
                                    
                                if(file.delete()) {
                                    System.out.println("file " + fileID + " deleted successfully");
                                } else {
                                    System.out.println("file " + fileID + " not deleted");
                                }
                            }

                        } else {
                            out.writeObject("File size exceeds server buffer capacity");
                        }

                        break;

                    case "7":
                        try {
                            logReader = new BufferedReader(new FileReader("./userData/" + user + "/log.txt"));

                            String lines = "";
                            while((line = logReader.readLine()) != null) {
                                lines += line;
                                lines += "\n";
                            }

                            out.writeObject(lines);

                            logReader.close();

                        } catch(FileNotFoundException e) {
                            System.out.println(e);
                        }
                    
                        break;

                    case "8":
                        users.put(user, false);
                        stopSession = true;
                        
                        break;
                    
                    default:
                        out.writeObject("\n!!!  Invalid operation  !!!");

                }
            }

        } catch (Exception e) {
            System.out.println("Error in session: " + e.getMessage());

        } finally {
            try {

                logWriter.close();
                userDataReader.close();
                userPrivateFileReader.close();
                userPrivateFileWriter.close();
                userPublicFileWriter.close();
                socket.close();

            } catch (IOException e) {
                System.out.println("Error closing socket: " + e.getMessage());
            }
        }
    }
}