package server;

public class ServerConfig {
    public Long MAX_BUFFER_SIZE = 0L;
    public Integer MAX_CHUNK_SIZE = 0; 
    public Integer MIN_CHUNK_SIZE = 0;
    public Long CURR_BUFFER_SIZE = 0L;
    public Integer MAX_USER_NUM = 0;
    public Integer CURR_USER_NUM = 0;
    public final Object serverConfigLock = new Object();
}