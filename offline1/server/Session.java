package server;
import java.io.*;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.*;
import java.time.LocalDateTime;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

public class Session extends Thread {
    private Socket socket;
    private boolean stopSession = false;
    private boolean authenticated = false;
    private BufferedWriter userListWriter = null;
    private Map<String, Boolean> users = null;
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
    private Long MAX_BUFFER_SIZE = 0L;
    private Integer MAX_CHUNK_SIZE = 200;
    private Integer MIN_CHUNK_SIZE = 100;
    private Long CURR_BUFFER_SIZE = 0L;
    private Integer FILE_NO = 0;
    private Long UPLOAD_SIZE = 0L;
    private Long DOWNLOAD_SIZE = 0L;
    private static final Object fileNoLock = new Object();
    private static final Object uploadLock = new Object();
    private static final Object logLock = new Object();
    private static final Object privateFileLock = new Object();
    private static final Object publicFileLock = new Object();
    private String line = null;
    private final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor(); 
    private ScheduledFuture<?> timeOutTask;

    public Session(Socket socket, 
                BufferedWriter userListWriter, 
                Map<String, Boolean> users,
                Map<String, String> files,
                Long MAX_BUFFER_SIZE,
                Integer MAX_CHUNK_SIZE,
                Integer MIN_CHUNK_SIZE,
                Long CURR_BUFFER_SIZE) {

        this.socket = socket;
        this.userListWriter = userListWriter;
        this.users = users;
        this.MAX_BUFFER_SIZE = MAX_BUFFER_SIZE;
        this.MAX_CHUNK_SIZE = MAX_CHUNK_SIZE;
        this.MIN_CHUNK_SIZE = MIN_CHUNK_SIZE;
        this.CURR_BUFFER_SIZE = CURR_BUFFER_SIZE;

        System.out.println("session " + MAX_CHUNK_SIZE + " " + MIN_CHUNK_SIZE);
        System.out.println("session " + this.MAX_CHUNK_SIZE + " " + this.MIN_CHUNK_SIZE);

    }

    private boolean Download(String user, String fileName) {
        try {
            BufferedInputStream readFile = new BufferedInputStream(new FileInputStream("./userData/" + user + "/" + fileName));
            
            AtomicBoolean timeOut = new AtomicBoolean(false);
            while(!timeOut.get()) {

                byte[] chunk = new byte[MAX_CHUNK_SIZE];

                Integer readBytes = readFile.read(chunk);
                out.writeObject(readBytes);

                if(readBytes == -1) {
                    out.writeObject("file download complete");
                    readFile.close();
                    return true;
                }

                out.writeObject(chunk);

                timeOutTask = scheduler.schedule(() -> {
                    timeOut.set(true);
                }, 10, TimeUnit.SECONDS);

                String response = (String) in.readObject();

                if(response != null && response.equalsIgnoreCase("received")) {

                    if(timeOutTask != null && !timeOutTask.isDone()) {
                        timeOutTask.cancel(false);
                    }

                }
            }

            out.writeObject("file download failed");
            readFile.close();
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

                    authenticated = true;
                }
            }

            users.put(user, true);
            out.writeObject("success");

            System.out.println("\nuser ===== " + user + " ===== logged into the server");

            logReader = new BufferedReader(new FileReader("./userData/" + user + "/log.txt"));
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
                }
            }
            userDataReader.close();

            userDataWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/userData.txt"));

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

                    case "6":
                        System.out.println("1.2");
                        String[] fileNameSize = ((String)in.readObject()).split(",");
                        System.out.println(fileNameSize[0] + " " + fileNameSize[1]);
                        
                        if(Long.parseLong(fileNameSize[1].trim()) + CURR_BUFFER_SIZE < MAX_BUFFER_SIZE) {
                            
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
                            int num = r.nextInt(MAX_CHUNK_SIZE - MIN_CHUNK_SIZE + 1) + MIN_CHUNK_SIZE;
                            System.out.println("num " + num);
                            
                            out.writeObject(fileID + ", " + Integer.toString(num));

                            File file = new File("./userData/" + user + "/" + fileNameSize[0]);
                            file.createNewFile();

                            BufferedOutputStream fileWrite = new BufferedOutputStream(new FileOutputStream(file));
                            Long uploaded = 0L;

                            System.out.println("1.1");

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
                                    try(BufferedWriter userDataWriter = new BufferedWriter(new FileWriter("./userData/" + user + "/userData.txt"))) {
                                        userDataWriter.write(String.format("""
                                                    upload_size, %d
                                                    download_size, %d
                                                    file_no, %d
                                                    """, UPLOAD_SIZE, DOWNLOAD_SIZE, FILE_NO));
                                    
                                    
                                        userDataWriter.newLine();                
                                        userDataWriter.flush();

                                    }    
                                    
                                }

                                synchronized(logLock) {
                                    logWriter.write(">>>>> upload,, ID " + fileID + ",, name " + fileNameSize[0] + ",, time " + LocalDateTime.now());
                                    logWriter.newLine();
                                    logWriter.flush();
                                }
                                
                            } else {

                                out.writeObject("file upload failed");
                                
                                if(file.delete()) {
                                    System.out.println("file " + fileID + " deleted successfully");
                                } else {
                                    System.out.println("file " + fileID + " not deleted");
                                }

                            }

                            fileWrite.close();

                        } else {
                            out.writeObject("File size exceeds server buffer capacity");
                        }


                        break;
                    case "9":
                        users.put(user, false);
                        stopSession = true;
                        
                        break;
                    
                    default:
                        out.writeObject("Invalid operation");

                }
            }

        } catch (Exception e) {
            System.out.println("Error in session: " + e.getMessage());

        } finally {
            try {

                logReader.close();
                logWriter.close();
                userDataReader.close();
                userDataWriter.close();
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